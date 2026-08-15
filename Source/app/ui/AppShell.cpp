#include "app/ui/AppShell.h"
#include "app/ui/DemoTrackCatalog.h"
#include "app/ui/NamLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace {
const juce::String kEllipsis = juce::String::fromUTF8 ("\xE2\x80\xA6");   // …
const juce::String kHeart = juce::String::fromUTF8 ("\xE2\x99\xA5");      // ♥
const juce::String kDotSep = juce::String::fromUTF8 ("\xC2\xB7");         // ·

// Filter vocabulary mirroring tone3000.com/search (captured 2026-08-10).
// Browse offers all of it via the real API params; favorites offers only
// what the kept deck actually contains (matched against display names).
struct GearDef {
    const char* display;
    const char* api;
};
const GearDef kGearVocab[] = {
    { "Amp + Cab", "amp-cab" },
    { "Amp Head", "amp" },
    { "Cabinet", "cab" },
    { "Pedal", "pedal" },
    { "Outboard", "outboard" },
    { "Spaces", "space" },
    { "Experimental", "experimental" },
};
const char* kTagVocab[] = { "nam",        "metal", "high-gain", "rock", "crunch",
                            "distortion", "clean", "guitar",    "ir",   "overdrive" };
const char* kMakeVocab[] = {
    "Shure SM57",  "Marshall JCM800", "Celestion", "EVH 5150", "Mesa Boogie Dual Rectifier",
    "Peavey 5150", "Laney",           "VOX AC30",  "Synergy",  "Ibanez TS9 Tube Screamer"
};
struct SortDef {
    const char* display;
    const char* api;
};
const SortDef kSortVocab[] = {
    { "Trending", "trending" },
    { "Newest", "newest" },
    { "Oldest", "oldest" },
    { "Best Match", "best-match" },
    { "Most Downloads", "downloads-all-time" },
};
}   // namespace

AppShell::AppShell (dsp::ToneEngine& engine) : engine_ (engine) {
    play_ = std::make_unique<PlayScreen> ();
    stacksHome_ = std::make_unique<StacksHomeScreen> ();
    stacksDetail_ = std::make_unique<StackDetailScreen> ();
    tuner_ = std::make_unique<TunerScreen> ();
    tuner_->setPanelMode (true);   // hosted as a card over Play, not a screen

    for (juce::Component* c :
         { (juce::Component*)play_.get (), (juce::Component*)stacksHome_.get (),
           (juce::Component*)stacksDetail_.get (), (juce::Component*)tuner_.get () })
        addChildComponent (*c);

    play_->onPrev = [this] { stepCollection (-1); };
    play_->onNext = [this] { stepCollection (+1); };
    play_->onSelectAbsolute = [this] (int i) {   // list/grid rows: full-deck index
        if (deckMode_ == 2) showBrowseCard (i);
        else showFavCard (i, true);
    };

    play_->onViewChange = [this] (int v) { setDeckMode (v); };

    // Layout picker (swipe / list / grid): list & grid are Pro (mode 0 is
    // the free swipe-card view). Gate before applying so a free tap never
    // silently switches layout underneath the paywall.
    play_->onViewTypeSelect = [this] (int mode) {
        if (kGatesEnabled && mode != 0 && isPro_ && !isPro_ ()) {
            openPaywall ("List & grid layouts");
            return;
        }
        play_->applyViewType (mode);
    };

    // Infinite browse: nearing the deck's end (scroll or swipe) appends the
    // next TONE3000 results page. Local decks already hold everything.
    play_->onDeckEndReached = [this] { fetchMoreBrowse (); };

    play_->onGearSelect = [this] (int idx) {
        if (deckMode_ != 2) return;
        browseGear_ = idx;
        runPlayBrowse ();   // fresh query restarts the infinite deck
    };

    play_->onFilterGroupsChanged = [this] (const std::vector<PlayScreen::FilterGroup>& groups) {
        if (deckMode_ == 2) {
            browseGroups_ = groups;
            runPlayBrowse ();   // fresh query restarts the infinite deck
        } else {
            favTags_.clear ();
            favMakes_.clear ();
            for (const auto& gp : groups) {
                if (gp.title == "TAGS") favTags_ = gp.selected;
                else if (gp.title == "MAKES & MODELS") favMakes_ = gp.selected;
            }
            showFavCard (0, false);
        }
    };

    play_->onKeepToggle = [this] {
        if (deckMode_ == 2) {
            if (playDeckIndex_ < 0 || playDeckIndex_ >= (int)playDeck_.size () || !svc_.keep)
                return;
            const auto tone = playDeck_[(size_t)playDeckIndex_];
            svc_.keep (tone, [this, tone] (bool ok, juce::String) {
                if (!ok) return;
                play_->setKept (svc_.isKept && svc_.isKept (tone.id));
                play_->setSaved (svc_.isSaved && svc_.isSaved (tone.id));
                updateCabChoices ();
            });
        } else {
            // Local decks: toggle the favorite flag (the download stays).
            const auto deck = favDeck ();
            if (collectionIndex_ < 0 || collectionIndex_ >= (int)deck.size () ||
                !svc_.setFavoriteById)
                return;
            const auto& e = deck[(size_t)collectionIndex_];
            svc_.setFavoriteById (e.id, !e.favorite);
            pushFilterGroups ();
            // Favorites deck: an un-hearted entry leaves; the next card
            // takes its slot. Downloaded deck: it stays, heart empties.
            showFavCard (collectionIndex_, deckMode_ == 0);
        }
    };

    play_->onSaveToggle = [this] {
        if (deckMode_ == 2) {
            if (playDeckIndex_ < 0 || playDeckIndex_ >= (int)playDeck_.size ()) return;
            const auto tone = playDeck_[(size_t)playDeckIndex_];
            const bool saved = svc_.isSaved && svc_.isSaved (tone.id);
            if (!saved) {
                if (!svc_.save) return;
                svc_.save (tone, [this, tone] (bool ok, juce::String) {
                    if (!ok) return;
                    play_->setSaved (true);
                    play_->setKept (svc_.isKept && svc_.isKept (tone.id));
                    updateCabChoices ();
                });
            } else if (svc_.removeKept && svc_.libraryIdForTone) {
                const auto id = svc_.libraryIdForTone (tone.id);
                if (!id.empty ()) svc_.removeKept (id);
                play_->setSaved (false);
                play_->setKept (false);
                updateCabChoices ();
            }
            return;
        }
        // Local decks: un-saving removes the entry from the device.
        const auto deck = favDeck ();
        if (collectionIndex_ < 0 || collectionIndex_ >= (int)deck.size () || !svc_.removeKept)
            return;
        svc_.removeKept (deck[(size_t)collectionIndex_].id);
        updateCabChoices ();
        pushFilterGroups ();
        showFavCard (collectionIndex_, true);
    };

    play_->onSelectPair = [this] (int i) {
        if (curCardIsCab_) {
            // PAIR AMP: load the chosen kept model under this cab (0 keeps
            // whatever amp is already running).
            if (i <= 0 || !loadModel_) return;
            const auto ms = keptModelsSorted ();
            if (i - 1 < (int)ms.size ()) loadModel_ (ms[(size_t)(i - 1)]);
            return;
        }
        // PAIR CAB: bundled cab or a kept IR.
        pairCabSel_ = i;
        if (i < cabBuiltinCount_) {
            if (svc_.setCab) svc_.setCab (i);
            return;
        }
        if (!getIrs_) return;
        const auto irs = getIrs_ ();
        const int k = i - cabBuiltinCount_;
        if (k >= 0 && k < (int)irs.size () && loadIr_) loadIr_ (irs[(size_t)k]);
    };

    play_->onSelectDemoTrack = [this] (int i) {
        // The first DI track is free; the rest of the library is Pro. Gate
        // BEFORE applying so a vetoed pick never desyncs the DEMO AUDIO
        // label from the track actually loaded.
        if (kGatesEnabled && i > 0 && isPro_ && !isPro_ ()) {
            openPaywall ("The full DI track library");
            return;
        }
        play_->applyDemoTrack (i);
        if (svc_.setDemoTrack) svc_.setDemoTrack (i, [] (bool) {});
    };

    play_->onToggleDemo = [this] {
        if (auditioningPack_ == -2) {   // Play-deck demo running -> stop
            if (svc_.stopDemo) svc_.stopDemo ();
            auditioningPack_ = -1;
            play_->setDemoPlaying (false);
            // Demo over: if the live input is the phone mic (system default,
            // no interface), mute it so the amp sim doesn't resume feeding
            // off room noise through the speaker.
            if (getDevices_ && muteInput_ &&
                getDevices_ ().currentInput.containsIgnoreCase ("System Default"))
                muteInput_ (true);
            return;
        }
        nam::ToneInfo tone;
        if (deckMode_ == 2) {
            if (playDeckIndex_ < 0 || playDeckIndex_ >= (int)playDeck_.size ()) return;
            tone = playDeck_[(size_t)playDeckIndex_];
        } else {
            const auto deck = favDeck ();
            if (collectionIndex_ < 0 || collectionIndex_ >= (int)deck.size ()) return;
            const auto& e = deck[(size_t)collectionIndex_];
            if (e.type == nam::LibraryType::Ir) {
                if (loadIr_) loadIr_ (e);
                return;
            }
            // Find the kept-tone info for this entry by display name (deck
            // may be filtered, so index mapping is unreliable).
            const auto kept = svc_.listKept ? svc_.listKept () : std::vector<nam::ToneInfo>{};
            bool found = false;
            for (const auto& k : kept)
                if (k.title == e.displayName) {
                    tone = k;
                    found = true;
                    break;
                }
            if (!found) return;
        }
        if (!svc_.audition) return;
        // Hitting play means "I want to hear this": lift a user output mute.
        if (outMuted_ && muteOutput_) muteOutput_ (false);
        svc_.audition (tone, [this] (bool ok, juce::String) {
            auditioningPack_ = ok ? -2 : -1;
            play_->setDemoPlaying (ok);
        });
    };
    wireStacksScreens ();   // Home/Detail callbacks (AppShellStacks.cpp)

    play_->onTuner = [this] { toggleTuner (); };
    tuner_->onBack = [this] {
        if (tunerOpen_) toggleTuner ();
    };
    addChildComponent (tunerScrim_);
    tunerScrim_.onTap = [this] {
        if (tunerOpen_) toggleTuner ();
    };
    addChildComponent (ioScrim_);
    ioScrim_.onTapAt = [this] (juce::Point<int> p) { handleIoPanelTap (p); };
    ioScrim_.onDragAt = [this] (juce::Point<int> p) { handleIoDragAt (p); };
    ioScrim_.onUpAt = [this] (juce::Point<int> p) { handleIoUpAt (p); };
    addChildComponent (moreScrim_);
    moreScrim_.onTapAt = [this] (juce::Point<int> p) {
        // DOWNLOADED: the deck moved out of the nav and lives here now.
        if (moreOpen_ && moreDownloadedRect_.contains (p)) {
            closeMoreMenu ();
            setDeckMode (1);
            return;
        }
        closeMoreMenu ();
    };

    // Card-back quick sliders -> engine (same ranges as the EDIT screen).
    play_->onToneParam = [this] (int idx, float v) {
        switch (idx) {
            case 0: engine_.setInputDb (-24.0f + v * 48.0f); break;
            case 1: engine_.setLowDb ((v - 0.5f) * 12.5f); break;
            case 2: engine_.setMidDb ((v - 0.5f) * 12.5f); break;
            case 3: engine_.setHighDb ((v - 0.5f) * 12.5f); break;
            case 4: engine_.setGateThresholdDb (-70.0f + v * 50.0f); break;
            default: break;
        }
    };

    show (Screen::Play);
}

void AppShell::setBrowseServices (BrowseServices services) {
    svc_ = std::move (services);

    // Saved stacks live behind the host's persistence service.
    loadStacksState ();
    pushStacks ();
}

void AppShell::stopAudition () {
    if (svc_.stopDemo) svc_.stopDemo ();
    auditioningPack_ = auditioningModel_ = -1;
    play_->setDemoPlaying (false);
}

// Kept for the host API; the audition progress bar left with BrowseScreen.
void AppShell::setAuditionProgress (float) {}

void AppShell::setLibraryService (GetModelsFn getModels, LoadModelFn loadModel) {
    getModels_ = std::move (getModels);
    loadModel_ = std::move (loadModel);
}

std::vector<nam::LibraryEntry> AppShell::favDeckAll () const {
    // Combined favorites deck: models + kept IRs, in the order they were
    // hearted (stable sort keeps store order for ties).
    std::vector<nam::LibraryEntry> deck;
    if (getModels_)
        for (auto& e : getModels_ ()) deck.push_back (e);
    if (getIrs_)
        for (auto& e : getIrs_ ()) deck.push_back (e);
    std::stable_sort (deck.begin (), deck.end (),
                      [] (const auto& a, const auto& b) { return a.addedAt < b.addedAt; });
    return deck;
}

std::vector<nam::LibraryEntry> AppShell::favDeck () const {
    // Apply the local-deck filters: mode scope (favorites vs all saved),
    // gear by entry type; tags/makes by display-name match (OR within a
    // group, AND across groups).
    auto deck = favDeckAll ();
    auto matches = [this] (const nam::LibraryEntry& e) {
        if (deckMode_ == 0 && !e.favorite) return false;
        if (favGear_ == 0 && e.type != nam::LibraryType::Model) return false;
        if (favGear_ == 1 && e.type != nam::LibraryType::Ir) return false;
        const auto name = juce::String (e.displayName).toLowerCase ();
        auto anyHit = [&name] (const juce::StringArray& terms) {
            for (const auto& t : terms)
                if (name.contains (t.toLowerCase ())) return true;
            return false;
        };
        if (!favTags_.isEmpty () && !anyHit (favTags_)) return false;
        if (!favMakes_.isEmpty () && !anyHit (favMakes_)) return false;
        return true;
    };
    deck.erase (
        std::remove_if (deck.begin (), deck.end (), [&] (const auto& e) { return !matches (e); }),
        deck.end ());
    return deck;
}

void AppShell::pushFilterGroups () {
    std::vector<PlayScreen::FilterGroup> groups;
    if (deckMode_ == 2) {
        // Full tone3000.com/search vocabulary via the real API parameters.
        // Gear lives in the strip dropdown, not the flyout.
        juce::StringArray gearChoices{ "All Gear" };
        for (const auto& gd : kGearVocab) gearChoices.add (gd.display);
        play_->setGearChoices (std::move (gearChoices));
        PlayScreen::FilterGroup tags{ "TAGS", {}, {}, false };
        for (const char* t : kTagVocab) tags.options.add (t);
        PlayScreen::FilterGroup makes{ "MAKES & MODELS", {}, {}, false };
        for (const char* m : kMakeVocab) makes.options.add (m);
        PlayScreen::FilterGroup tech{ "TECHNICAL", { "Any", "A2", "A1" }, { "Any" }, true };
        PlayScreen::FilterGroup sort{ "SORT", {}, { "Trending" }, true };
        for (const auto& sd : kSortVocab) sort.options.add (sd.display);
        groups = { tags, makes, tech, sort };
        browseGroups_ = groups;
    } else {
        // Local decks: only offer chips the deck can actually satisfy (the
        // first word of a make is enough to match against display names).
        // Favorites mode derives from favorites only.
        auto deck = favDeckAll ();
        if (deckMode_ == 0)
            deck.erase (std::remove_if (deck.begin (), deck.end (),
                                        [] (const auto& e) { return !e.favorite; }),
                        deck.end ());
        PlayScreen::FilterGroup tags{ "TAGS", {}, {}, false };
        for (const char* t : kTagVocab)
            for (const auto& e : deck)
                if (juce::String (e.displayName).containsIgnoreCase (t)) {
                    tags.options.add (t);
                    break;
                }
        PlayScreen::FilterGroup makes{ "MAKES & MODELS", {}, {}, false };
        for (const char* m : kMakeVocab) {
            const auto word = juce::String (m).upToFirstOccurrenceOf (" ", false, false);
            for (const auto& e : deck)
                if (juce::String (e.displayName).containsIgnoreCase (word)) {
                    makes.options.add (word);
                    break;
                }
        }
        makes.options.removeDuplicates (true);
        groups = { tags, makes };
    }
    play_->setFilterGroups (std::move (groups));
}

void AppShell::showFavCard (int index, bool loadIntoEngine) {
    const auto deck = favDeck ();
    if (deck.empty ()) {
        collectionIndex_ = -1;
        play_->setNowPlaying ("Nothing kept yet", "NAM PLAYER", {});
        play_->setArtwork ({});
        play_->setKept (false);
        play_->setPosition (-1, 0);
        pushDeckItems ();
        play_->setActiveDeckIndex (-1);
        return;
    }
    const int n = (int)deck.size ();
    index = ((index % n) + n) % n;
    collectionIndex_ = index;
    const auto& e = deck[(size_t)index];
    const bool isIr = (e.type == nam::LibraryType::Ir);
    if (loadIntoEngine) {
        if (isIr) {
            if (loadIr_) loadIr_ (e);
        }   // swap the cab, keep the amp
        else {
            if (loadModel_) loadModel_ (e);
        }
    }
    juce::String family ("MY TONES");
    if (isIr) {
        family = "CABINET IR " + kDotSep + " MY TONES";
    } else if (!e.arch.empty ()) {
        const auto a = juce::String (e.arch).toLowerCase ();
        const bool isA2 = a.contains ("slim") || a.startsWith ("2") || a.startsWith ("a2");
        family = juce::String (isA2 ? "A2" : "A1") + " " + kDotSep + " MY TONES";
    }
    play_->setNowPlaying (juce::String (e.displayName), family, {});
    play_->setArtwork (artwork_ ? artwork_ (e) : juce::Image ());
    play_->setKept (e.favorite);
    play_->setSaved (true);
    play_->setCabCard (isIr);
    if (curCardIsCab_ != isIr) {
        curCardIsCab_ = isIr;
        pushPairChoices ();
    }
    play_->setPosition (index, n);
    pushDeckItems ();
    play_->setActiveDeckIndex (index);
}

void AppShell::showBrowseCard (int index) {
    if (playDeck_.empty ()) {
        playDeckIndex_ = -1;
        play_->setNowPlaying ("No results", "TONE3000", {});
        play_->setArtwork ({});
        play_->setKept (false);
        play_->setPosition (-1, 0);
        pushDeckItems ();
        play_->setActiveDeckIndex (-1);
        return;
    }
    const int n = (int)playDeck_.size ();
    index = ((index % n) + n) % n;
    playDeckIndex_ = index;
    const auto& t = playDeck_[(size_t)index];
    play_->setNowPlaying (
        juce::String (t.title),
        (t.gear.empty () ? juce::String ("TONE3000")
                         : juce::String (t.gear).toUpperCase () + " " + kDotSep + " TONE3000"),
        {});
    play_->setArtwork (svc_.artworkForTone ? svc_.artworkForTone (t) : juce::Image ());
    play_->setKept (svc_.isKept && svc_.isKept (t.id));
    play_->setSaved (svc_.isSaved && svc_.isSaved (t.id));
    const bool cab = (t.format == "ir" || t.gear == "cab");
    play_->setCabCard (cab);
    if (curCardIsCab_ != cab) {
        curCardIsCab_ = cab;
        pushPairChoices ();
    }
    play_->setPosition (index, n);
    pushDeckItems ();
    play_->setActiveDeckIndex (index);
    // Swiping into the last few cards appends the next page (infinite).
    if (index >= n - 3) fetchMoreBrowse ();
}

void AppShell::fetchMoreBrowse () {
    // Append the NEXT TONE3000 page to the live deck. Guarded: one fetch in
    // flight, stop once the server runs dry, and cap the deck (house rule:
    // nothing unbounded). No SSE/streaming exists on the API — it is plain
    // page-numbered REST, so "get them all" = fetch pages as needed.
    if (deckMode_ != 2 || browseFetching_ || browseExhausted_ || !svc_.searchEx) return;
    if (!browseLoaded_) return;   // page 1 of THIS query hasn't landed yet
    if ((int)playDeck_.size () >= kBrowseDeckCap) return;
    browseFetching_ = true;
    auto p = buildBrowseParams ();
    p.page = browsePage_ + 1;
    svc_.searchEx (p, [this, page = p.page,
                       gen = browseGen_] (bool ok, std::vector<nam::ToneInfo> tones, juce::String) {
        browseFetching_ = false;
        // A fresh query replaced the deck while this page was
        // in flight — appending would mix two searches. Drop.
        if (!ok || deckMode_ != 2 || gen != browseGen_) return;
        browsePage_ = page;
        if ((int)tones.size () < 25) browseExhausted_ = true;   // short page = the end
        for (auto& t : tones) playDeck_.push_back (std::move (t));
        pushDeckItems ();
        if (playDeckIndex_ >= 0) play_->setPosition (playDeckIndex_, (int)playDeck_.size ());
    });
}

nam::SearchParams AppShell::buildBrowseParams () const {
    // Real /tones/search params from the strip + flyout state.
    nam::SearchParams p;
    p.sort = "trending";
    const int gi = browseGear_ - 1;   // dropdown index 0 = All Gear
    if (gi >= 0 && gi < (int)(sizeof (kGearVocab) / sizeof (kGearVocab[0])))
        p.gears.push_back (kGearVocab[gi].api);
    for (const auto& gp : browseGroups_) {
        if (gp.title == "TAGS") {
            for (const auto& sel : gp.selected) p.tags.push_back (sel.toStdString ());
        } else if (gp.title == "MAKES & MODELS") {
            for (const auto& sel : gp.selected) p.makes.push_back (sel.toStdString ());
        } else if (gp.title == "TECHNICAL" && !gp.selected.isEmpty ()) {
            p.architecture = gp.selected[0] == "A2" ? 2 : gp.selected[0] == "A1" ? 1 : 0;
        } else if (gp.title == "SORT" && !gp.selected.isEmpty ()) {
            for (const auto& sd : kSortVocab)
                if (gp.selected[0] == sd.display) p.sort = sd.api;
        }
    }
    return p;
}

void AppShell::runPlayBrowse () {
    if (!svc_.searchEx) return;
    // A fresh query restarts the infinite deck at page 1 and invalidates
    // any in-flight append (generation token).
    browsePage_ = 1;
    browseExhausted_ = false;
    browseLoaded_ = false;   // appends hold off until page 1 replaces the deck
    ++browseGen_;
    auto p = buildBrowseParams ();
    p.page = 1;
    svc_.searchEx (
        p, [this, gen = browseGen_] (bool ok, std::vector<nam::ToneInfo> tones, juce::String) {
            if (!ok || gen != browseGen_) return;   // superseded by a newer query
            browseLoaded_ = true;
            playDeck_ = std::move (tones);
            playDeckIndex_ = playDeck_.empty () ? -1 : 0;
            if (deckMode_ == 2) showBrowseCard (0);
        });
}

void AppShell::updateCabChoices () {
    cabChoiceNames_.clear ();
    for (int c = 0; c < nam::demo::kNumCabs; ++c)
        cabChoiceNames_.add (nam::demo::kCabs[(size_t)c].display);
    cabBuiltinCount_ = cabChoiceNames_.size ();
    if (getIrs_)
        for (const auto& e : getIrs_ ()) cabChoiceNames_.add (juce::String (e.displayName));
    pushPairChoices ();
}

std::vector<nam::LibraryEntry> AppShell::keptModelsSorted () const {
    auto ms = getModels_ ? getModels_ () : std::vector<nam::LibraryEntry>{};
    std::stable_sort (ms.begin (), ms.end (),
                      [] (const auto& a, const auto& b) { return a.addedAt < b.addedAt; });
    return ms;
}

void AppShell::pushPairChoices () {
    if (curCardIsCab_) {
        // Cab card: choose which amp head drives it (TONE3000 cab pages do
        // the same). Options come from the kept/downloaded models.
        juce::StringArray names{ "Current amp" };
        for (const auto& e : keptModelsSorted ()) names.add (juce::String (e.displayName));
        play_->setPairChoices ("PAIR AMP", std::move (names), 0);
    } else {
        play_->setPairChoices ("PAIR CAB", cabChoiceNames_, pairCabSel_);
    }
}

void AppShell::setDeckMode (int v) {
    if (current_ != play_.get ()) show (Screen::Play);
    if (v == deckMode_) {
        repaint (navBar_);
        return;
    }
    deckMode_ = v;
    play_->setDeckView (v);
    favGear_ = -1;
    favTags_.clear ();
    favMakes_.clear ();
    browseGear_ = 0;
    pushFilterGroups ();
    if (svc_.stopDemo) svc_.stopDemo ();
    play_->setDemoPlaying (false);
    if (deckMode_ == 2) runPlayBrowse ();
    else showFavCard (0, false);
    repaint (navBar_);
}

void AppShell::showEntryAsNowPlaying (const nam::LibraryEntry& e) {
    auditionToneId_.clear ();   // a library entry owns the engine now
    deckMode_ = 0;
    play_->setDeckView (0);
    const auto deck = favDeck ();
    for (size_t i = 0; i < deck.size (); ++i)
        if (deck[i].id == e.id && deck[i].type == e.type) {
            showFavCard ((int)i, false);
            return;
        }
    collectionIndex_ = -1;
    play_->setNowPlaying (juce::String (e.displayName), "MY TONES", {});
    play_->setArtwork (artwork_ ? artwork_ (e) : juce::Image ());
    play_->setKept (false);
    play_->setPosition (-1, 0);
}

void AppShell::setNowPlayingInfo (juce::String name, juce::String family) {
    collectionIndex_ = -1;
    play_->setNowPlaying (std::move (name), std::move (family), {});
    play_->setArtwork ({});
    play_->setPosition (-1, 0);
}

void AppShell::stepCollection (int delta) {
    if (deckMode_ == 2) {
        showBrowseCard (playDeckIndex_ < 0 ? (delta > 0 ? 0 : -1) : playDeckIndex_ + delta);
        return;
    }
    showFavCard (collectionIndex_ < 0 ? (delta > 0 ? 0 : -1) : collectionIndex_ + delta, true);
}

void AppShell::setAudioDeviceService (GetDevicesFn get, SelectDeviceFn selectInput,
                                      SelectDeviceFn selectOutput, RescanFn rescan,
                                      SelectDeviceFn selectRate, SelectDeviceFn selectBuffer) {
    getDevices_ = std::move (get);
    selectInput_ = std::move (selectInput);
    selectOutput_ = std::move (selectOutput);
    rescanDevices_ = std::move (rescan);
    selectRate_ = std::move (selectRate);
    selectBuffer_ = std::move (selectBuffer);
}

void AppShell::setProServices (std::function<bool ()> isPro, std::function<void (DoneFn)> purchase,
                               std::function<void (DoneFn)> restore) {
    isPro_ = std::move (isPro);
    purchasePro_ = std::move (purchase);
    restorePro_ = std::move (restore);
    refreshProState ();   // push the initial lock-glyph state to PlayScreen
}

void AppShell::setProPriceText (juce::String price) {
    if (price.isEmpty ()) return;   // ignore a malformed/empty store answer
    proPriceText_ = "UNLOCK " + kDotSep + " " + price;
    if (paywall_ != nullptr) paywall_->setPriceText (proPriceText_);
}

void AppShell::openPaywall (const juce::String& reason) {
    if (paywall_ == nullptr) {
        paywall_ = std::make_unique<PaywallPanel> ();
        addChildComponent (*paywall_);
        paywall_->onClose = [this] { dismissPaywall (); };
        paywall_->onBuy = [this] {
            // proCallInFlight_ also gates the buttons themselves (see below),
            // but a defensive re-check here means a stray onBuy call can
            // never fire a second purchaseProduct() while one is unresolved.
            if (!purchasePro_ || proCallInFlight_) return;
            proCallInFlight_ = true;
            paywall_->setBusy (true);
            purchasePro_ ([this] (bool, juce::String status) {
                proCallInFlight_ = false;
                if (paywall_ == nullptr) return;
                paywall_->setBusy (false);
                paywall_->setStatus (status);
                refreshProState ();
            });
        };
        paywall_->onRestore = [this] {
            if (!restorePro_ || proCallInFlight_) return;
            proCallInFlight_ = true;
            paywall_->setBusy (true);
            restorePro_ ([this] (bool, juce::String status) {
                proCallInFlight_ = false;
                if (paywall_ == nullptr) return;
                paywall_->setBusy (false);
                paywall_->setStatus (status);
                refreshProState ();
            });
        };
    }
    // A prior purchase/restore may still be outstanding from before the
    // sheet was dismissed — reopening must NOT re-arm BUY/RESTORE while that
    // call's DoneFn hasn't landed yet (it would let a second tap fire a
    // second store call and drop the first callback).
    paywall_->setBusy (proCallInFlight_);
    if (!proCallInFlight_) paywall_->setStatus ({});
    paywall_->setReason (reason);
    if (proPriceText_.isNotEmpty ()) paywall_->setPriceText (proPriceText_);
    paywall_->setBounds (getLocalBounds ());
    paywall_->setVisible (true);
    paywall_->toFront (false);
}

void AppShell::dismissPaywall () {
    if (paywall_ == nullptr) return;
    paywall_->setVisible (false);
}

void AppShell::refreshProState () {
    const bool pro = isPro_ && isPro_ ();
    if (pro) dismissPaywall ();
    // Lock glyphs: a null service means ungated (desktop/dev builds), same
    // convention as the gate checks above, so PlayScreen must see "unlocked"
    // rather than the isPro_() default of false.
    if (play_) play_->setProState (!kGatesEnabled || !isPro_ || pro);
    repaint ();
}

bool AppShell::handleBackButton () {
    // Same dismissal as the sheet's own close button (onClose): busy state
    // deliberately survives this per the timeout fix, it just stops being
    // shown until the sheet is reopened.
    if (paywall_ != nullptr && paywall_->isVisible ()) {
        dismissPaywall ();
        return true;
    }
    if (tunerOpen_) {
        toggleTuner ();
        return true;
    }
    // An open Detail overlay (item sheet / gear picker / EDIT's REMOVE
    // STACK confirm) dismisses first, same as the paywall above -- without
    // this, back-ing out of an open confirm and reopening a DIFFERENT
    // stack left the dialog showing over it (review finding on 06d4e74).
    if (current_ == stacksDetail_.get () && stacksDetail_ != nullptr &&
        stacksDetail_->closeTopOverlay ())
        return true;
    // Stack detail backs out to Home first, matching its own ‹ chevron —
    // only a second back press (or one from Home) leaves Stacks for Play.
    if (current_ == stacksDetail_.get ()) {
        stacksShowDetail_ = false;
        show (Screen::Stacks);
        return true;
    }
    if (current_ != nullptr && current_ != play_.get ()) {
        show (Screen::Play);
        return true;
    }
    return false;
}

void AppShell::toggleTuner () {
    if (play_ == nullptr || tuner_ == nullptr) return;
    auto& animator = juce::Desktop::getInstance ().getAnimator ();
    // Grows out of the Play tuner panel; the panel itself stays exposed
    // below the overlay as the collapse handle. Content-sized card, same
    // width/border language as the panel.
    const auto panel = play_->tunerPanelBounds () + play_->getPosition ();
    const int h = juce::jmin (TunerScreen::kPanelHeight, panel.getY () - play_->getY () - 16);
    const juce::Rectangle<int> expanded{ panel.getX (), panel.getY () - 8 - h, panel.getWidth (),
                                         h };
    if (!tunerOpen_) {
        tunerOpen_ = true;
        animator.cancelAnimation (tuner_.get (), false);
        tuner_->setAlpha (1.0f);
        tuner_->setBounds (panel);
        tuner_->setVisible (true);
        tunerScrim_.setBounds (contentBounds ());
        tunerScrim_.setVisible (true);
        tunerScrim_.toFront (false);
        tuner_->toFront (false);
        animator.animateComponent (tuner_.get (), expanded, 1.0f, 220, false, 1.0, 0.0);
    } else {
        tunerOpen_ = false;
        tunerScrim_.setVisible (false);
        animator.animateComponent (tuner_.get (), panel, 0.0f, 180, false, 1.0, 0.0);
        juce::Timer::callAfterDelay (
            200, [this, safe = juce::Component::SafePointer<TunerScreen> (tuner_.get ())] {
                if (safe == nullptr || tunerOpen_) return;
                safe->setVisible (false);
                safe->setAlpha (1.0f);
            });
    }
}

void AppShell::show (Screen s) {
    closeIoPanel ();
    closeMoreMenu ();
    if (paywall_ != nullptr) paywall_->setVisible (false);

    // The tuner overlay belongs to Play, the only screen that opens it. Only
    // a same-screen Play re-selection is exempt from closing it; any other
    // target must close it, or it's left floating behind the next opaque
    // screen with tunerOpen_ still true -- a stray BACK press then "closes"
    // a tuner nothing on screen shows, and Play's own tuner tap needs two
    // presses to reopen it.
    const bool sameScreenPlayReopen = (s == Screen::Play && current_ == play_.get ());
    if (tunerOpen_ && !sameScreenPlayReopen) {
        juce::Desktop::getInstance ().getAnimator ().cancelAnimation (tuner_.get (), false);
        tuner_->setVisible (false);
        tuner_->setAlpha (1.0f);
        tunerScrim_.setVisible (false);
        tunerOpen_ = false;
    }

    // current_ swaps to the new target BEFORE the pushStacks() refresh below
    // -- its retry-visibility gate reads current_, previously stale here.
    juce::Component* target = (s == Screen::Stacks)
                                  ? (stacksShowDetail_ ? (juce::Component*)stacksDetail_.get ()
                                                       : (juce::Component*)stacksHome_.get ())
                                  : (juce::Component*)play_.get ();
    if (current_ != target) {
        if (current_ != nullptr) current_->setVisible (false);
        current_ = target;
        current_->setBounds (contentBounds ());
        current_->setVisible (true);
        current_->toFront (false);
    }
    repaint (navBar_);                        // active nav highlight tracks the visible screen
    if (s == Screen::Stacks) pushStacks ();   // refresh data-backed screens

    // Arriving at Play with an audition tone still in the engine: if that
    // tone was hearted into the deck, promote it to the ACTIVE library entry
    // (real load — position, artwork, and boot persistence follow). If it
    // was never kept the engine keeps running it live; nothing to reload.
    if (s == Screen::Play && !auditionToneId_.empty ()) {
        const auto toneId = auditionToneId_;
        auditionToneId_.clear ();
        if (getModels_ && loadModel_ && svc_.libraryIdForTone) {
            const auto libId = svc_.libraryIdForTone (toneId);
            if (!libId.empty ())
                for (const auto& e : getModels_ ())
                    if (e.id == libId) {
                        loadModel_ (e);
                        showEntryAsNowPlaying (e);
                        break;
                    }
        }
    }

    // Stacks tracks what's actually live by id (liveModelToneId_/
    // liveIrToneId_, see AppShell.h) so applyStackToEngine can skip a load
    // that's already audible. Only its own applies update those ids --
    // Play-side loads (loadModel_/loadIr_/setCab) don't, so without this a
    // Play-side swap while Stacks is off-screen leaves the ids stale and a
    // later Stacks re-entry skips the load it should make, silently leaving
    // the wrong tone live under the editor. Every Play-side load requires
    // Play visible, so clearing both whenever Play becomes the nav target
    // closes that gap; worst case is one redundant reload (harmless).
    if (s == Screen::Play) {
        liveModelToneId_.clear ();
        liveIrToneId_.clear ();
    }
}

void AppShell::setLevels (float in, float out) {
    if (play_ != nullptr) play_->setLevels (in, out);
    if (std::abs (in - meterInPeak_) > 0.005f || std::abs (out - meterOutPeak_) > 0.005f) {
        meterInPeak_ = in;
        meterOutPeak_ = out;
        repaint (orbRect_.expanded (4));
        if (ioPanelOpen_) repaint (ioPanelRect_);
    }
}

void AppShell::setLatencyMs (double ms) {
    if (std::abs (ms - latencyMs_) < 0.05) return;
    latencyMs_ = ms;
    repaint (orbRect_);
}

void AppShell::setIoMuted (bool inMuted, bool outMuted) {
    if (inMuted == inMuted_ && outMuted == outMuted_) return;
    inMuted_ = inMuted;
    outMuted_ = outMuted;
    repaint (orbRect_.expanded (4));
    if (ioPanelOpen_) repaint (ioPanelRect_);
}

void AppShell::setTunerPitch (float hz) {
    // Hold the Play-panel reading through short detection dropouts (~1 s at
    // the 10 Hz feed) so a decaying note fades out instead of cutting out.
    if (hz <= 0.0f) {
        if (tunerMiss_ < 10 && ++tunerMiss_ >= 10) play_->setTuner ({}, 0.0f, false);
        if (tuner_ != nullptr) tuner_->setPitch (0.0f);
        return;
    }
    tunerMiss_ = 0;
    if (play_ == nullptr) return;
    if (tuner_ != nullptr) tuner_->setPitch (hz);
    static const char* names[] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    const double midi = 69.0 + 12.0 * std::log2 ((double)hz / 440.0);
    const int nearest = (int)std::round (midi);
    const float cents = (float)((midi - (double)nearest) * 100.0);
    const int nameIdx = ((nearest % 12) + 12) % 12;
    const int octave = nearest / 12 - 1;
    play_->setTuner (juce::String (names[nameIdx]) + juce::String (octave), cents, true);
}

juce::Rectangle<int> AppShell::contentBounds () const {
    auto b = getLocalBounds ();
    b.removeFromBottom (navBar_.getHeight ());
    return b;
}

void AppShell::resized () {
    auto b = getLocalBounds ();
    // Bottom chrome: BROWSE / FAVORITES | status orb | DOWNLOADED / STACKS.
    navBar_ = b.removeFromBottom (juce::jmax (72, getHeight () / 12));
    const int orbD = juce::jmin (58, navBar_.getHeight () - 10);
    orbRect_ = { navBar_.getCentreX () - orbD / 2, navBar_.getCentreY () - orbD / 2, orbD, orbD };
    {
        auto left =
            navBar_.withTrimmedRight (navBar_.getWidth () / 2 + orbD / 2 + 6).withTrimmedLeft (8);
        auto right =
            navBar_.withTrimmedLeft (navBar_.getWidth () / 2 + orbD / 2 + 6).withTrimmedRight (8);
        navBrowseRect_ = left.removeFromLeft (left.getWidth () / 2);
        navFavRect_ = left;
        navStacksRect_ = right.removeFromLeft (right.getWidth () / 2);
        navMoreRect_ = right;
    }

    // I/O panel floats above the nav, centred on the orb: ENGINE row
    // (rate/buffer pickers), INPUT, OUTPUT, then the TEST TONE row.
    const int pw = juce::jmin (380, getWidth () - 48);
    ioPanelRect_ = { getWidth () / 2 - pw / 2, navBar_.getY () - 248 - 10, pw, 248 };
    auto rows = ioPanelRect_.reduced (14, 12);
    ioEngRow_ = rows.removeFromTop (52);
    rows.removeFromTop (8);
    ioInRow_ = rows.removeFromTop (52);
    rows.removeFromTop (8);
    ioOutRow_ = rows.removeFromTop (52);
    ioTestRect_ = rows.removeFromBottom (36).reduced (rows.getWidth () / 4, 0);
    {
        auto pills = ioEngRow_.reduced (14, 6).removeFromRight (150).withTrimmedTop (10);
        ioRatePill_ = pills.removeFromLeft (70).withSizeKeepingCentre (70, 28);
        ioBufPill_ = pills.removeFromRight (70).withSizeKeepingCentre (70, 28);
    }

    for (juce::Component* c :
         { (juce::Component*)play_.get (), (juce::Component*)stacksHome_.get (),
           (juce::Component*)stacksDetail_.get () })
        if (c != nullptr) c->setBounds (b);
    if (paywall_ != nullptr) paywall_->setBounds (getLocalBounds ());

    // The tuner overlay tracks the Play tuner panel, not the screen grid.
    if (tuner_ != nullptr && tunerOpen_) {
        const auto panel = play_->tunerPanelBounds () + play_->getPosition ();
        const int h = juce::jmin (TunerScreen::kPanelHeight, panel.getY () - play_->getY () - 16);
        tuner_->setBounds ({ panel.getX (), panel.getY () - 8 - h, panel.getWidth (), h });
        tunerScrim_.setBounds (contentBounds ());
    }
}
