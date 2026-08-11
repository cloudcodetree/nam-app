#include "app/ui/AppShell.h"
#include "app/ui/DemoTrackCatalog.h"
#include "app/ui/NamLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace {
const juce::String kEllipsis = juce::String::fromUTF8 ("\xE2\x80\xA6"); // …
const juce::String kHeart    = juce::String::fromUTF8 ("\xE2\x99\xA5"); // ♥
const juce::String kDotSep   = juce::String::fromUTF8 ("\xC2\xB7");     // ·

// Filter vocabulary mirroring tone3000.com/search (captured 2026-08-10).
// Browse offers all of it via the real API params; favorites offers only
// what the kept deck actually contains (matched against display names).
struct GearDef { const char* display; const char* api; };
const GearDef kGearVocab[] = {
    { "Amp + Cab", "amp-cab" }, { "Amp Head", "amp" }, { "Cabinet", "cab" },
    { "Pedal", "pedal" }, { "Outboard", "outboard" }, { "Spaces", "space" },
    { "Experimental", "experimental" },
};
const char* kTagVocab[] = { "nam", "metal", "high-gain", "rock", "crunch",
                            "distortion", "clean", "guitar", "ir", "overdrive" };
const char* kMakeVocab[] = { "Shure SM57", "Marshall JCM800", "Celestion",
                             "EVH 5150", "Mesa Boogie Dual Rectifier",
                             "Peavey 5150", "Laney", "VOX AC30", "Synergy",
                             "Ibanez TS9 Tube Screamer" };
struct SortDef { const char* display; const char* api; };
const SortDef kSortVocab[] = {
    { "Trending", "trending" }, { "Newest", "newest" }, { "Oldest", "oldest" },
    { "Best Match", "best-match" }, { "Most Downloads", "downloads-all-time" },
};
}

AppShell::AppShell (dsp::ToneEngine& engine) : engine_ (engine) {
    play_    = std::make_unique<PlayScreen>();
    edit_    = std::make_unique<EditScreen> (engine_);
    browse_  = std::make_unique<BrowseScreen>();
    library_ = std::make_unique<LibraryScreen>();
    live_    = std::make_unique<LiveScreen>();
    devices_ = std::make_unique<AudioSettingsScreen>();
    stacks_  = std::make_unique<StacksScreen>();
    tuner_   = std::make_unique<TunerScreen>();
    tuner_->setPanelMode (true);   // hosted as a card over Play, not a screen

    for (juce::Component* c : { (juce::Component*) play_.get(), (juce::Component*) edit_.get(),
                                (juce::Component*) browse_.get(), (juce::Component*) library_.get(),
                                (juce::Component*) live_.get(), (juce::Component*) devices_.get(),
                                (juce::Component*) stacks_.get(),
                                (juce::Component*) tuner_.get() })
        addChildComponent (*c);

    play_->onLibrary  = [this] { show (Screen::Library); };
    play_->onSettings = [this] { show (Screen::Devices); };
    play_->onEdit     = [this] { show (Screen::Edit); };
    play_->onLive     = [this] { show (Screen::Live); };
    play_->onPrev     = [this] { stepCollection (-1); };
    play_->onNext     = [this] { stepCollection (+1); };
    play_->onSelectIndex = [this] (int k) {
        if (deckMode_ == 2) showBrowseCard (k);
        else                showFavCard (deckWindow_ * 25 + k, true);   // dots are windowed
    };

    play_->onSelectAbsolute = [this] (int i) {   // list/grid rows: full-deck index
        if (deckMode_ == 2) showBrowseCard (i);
        else                showFavCard (i, true);
    };

    play_->onViewChange = [this] (int v) { setDeckMode (v); };

    play_->onPageDelta = [this] (int d) {
        if (deckMode_ == 2) {
            browsePage_ = juce::jmax (1, browsePage_ + d);
            runPlayBrowse();
        } else {
            const int n = (int) favDeck().size();
            const int target = (deckWindow_ + d) * 25;
            if (target >= 0 && target < n) showFavCard (target, true);
        }
    };

    play_->onGearSelect = [this] (int idx) {
        if (deckMode_ != 2) return;
        browseGear_ = idx;
        browsePage_ = 1;   // fresh query, fresh pages
        runPlayBrowse();
    };

    play_->onFilterGroupsChanged = [this] (const std::vector<PlayScreen::FilterGroup>& groups) {
        if (deckMode_ == 2) {
            browseGroups_ = groups;
            browsePage_ = 1;   // fresh query, fresh pages
            runPlayBrowse();
        } else {
            favTags_.clear();
            favMakes_.clear();
            for (const auto& gp : groups) {
                if (gp.title == "TAGS")               favTags_ = gp.selected;
                else if (gp.title == "MAKES & MODELS") favMakes_ = gp.selected;
            }
            showFavCard (0, false);
        }
    };

    play_->onKeepToggle = [this] {
        if (deckMode_ == 2) {
            if (playDeckIndex_ < 0 || playDeckIndex_ >= (int) playDeck_.size()
                || ! svc_.keep)
                return;
            const auto tone = playDeck_[(size_t) playDeckIndex_];
            svc_.keep (tone, [this, tone] (bool ok, juce::String) {
                if (! ok) return;
                play_->setKept (svc_.isKept && svc_.isKept (tone.id));
                play_->setSaved (svc_.isSaved && svc_.isSaved (tone.id));
                updateCabChoices();
            });
        } else {
            // Local decks: toggle the favorite flag (the download stays).
            const auto deck = favDeck();
            if (collectionIndex_ < 0 || collectionIndex_ >= (int) deck.size()
                || ! svc_.setFavoriteById)
                return;
            const auto& e = deck[(size_t) collectionIndex_];
            svc_.setFavoriteById (e.id, ! e.favorite);
            pushFilterGroups();
            // Favorites deck: an un-hearted entry leaves; the next card
            // takes its slot. Downloaded deck: it stays, heart empties.
            showFavCard (collectionIndex_, deckMode_ == 0);
        }
    };

    play_->onSaveToggle = [this] {
        if (deckMode_ == 2) {
            if (playDeckIndex_ < 0 || playDeckIndex_ >= (int) playDeck_.size()) return;
            const auto tone = playDeck_[(size_t) playDeckIndex_];
            const bool saved = svc_.isSaved && svc_.isSaved (tone.id);
            if (! saved) {
                if (! svc_.save) return;
                svc_.save (tone, [this, tone] (bool ok, juce::String) {
                    if (! ok) return;
                    play_->setSaved (true);
                    play_->setKept (svc_.isKept && svc_.isKept (tone.id));
                    updateCabChoices();
                });
            } else if (svc_.removeKept && svc_.libraryIdForTone) {
                const auto id = svc_.libraryIdForTone (tone.id);
                if (! id.empty()) svc_.removeKept (id);
                play_->setSaved (false);
                play_->setKept (false);
                updateCabChoices();
            }
            return;
        }
        // Local decks: un-saving removes the entry from the device.
        const auto deck = favDeck();
        if (collectionIndex_ < 0 || collectionIndex_ >= (int) deck.size()
            || ! svc_.removeKept)
            return;
        svc_.removeKept (deck[(size_t) collectionIndex_].id);
        updateCabChoices();
        pushFilterGroups();
        showFavCard (collectionIndex_, true);
    };

    play_->onSelectPair = [this] (int i) {
        if (curCardIsCab_) {
            // PAIR AMP: load the chosen kept model under this cab (0 keeps
            // whatever amp is already running).
            if (i <= 0 || ! loadModel_) return;
            const auto ms = keptModelsSorted();
            if (i - 1 < (int) ms.size()) loadModel_ (ms[(size_t) (i - 1)]);
            return;
        }
        // PAIR CAB: bundled cab or a kept IR.
        pairCabSel_ = i;
        if (i < cabBuiltinCount_) { if (svc_.setCab) svc_.setCab (i); return; }
        if (! getIrs_) return;
        const auto irs = getIrs_();
        const int k = i - cabBuiltinCount_;
        if (k >= 0 && k < (int) irs.size() && loadIr_) loadIr_ (irs[(size_t) k]);
    };

    play_->onSelectDemoTrack = [this] (int i) {
        if (svc_.setDemoTrack) svc_.setDemoTrack (i, [] (bool) {});
    };

    play_->onToggleDemo = [this] {
        if (auditioningPack_ == -2) {   // Play-deck demo running -> stop
            if (svc_.stopDemo) svc_.stopDemo();
            auditioningPack_ = -1;
            play_->setDemoPlaying (false);
            // Demo over: if the live input is the phone mic (system default,
            // no interface), mute it so the amp sim doesn't resume feeding
            // off room noise through the speaker.
            if (getDevices_ && muteInput_
                && getDevices_().currentInput.containsIgnoreCase ("System Default"))
                muteInput_ (true);
            return;
        }
        nam::ToneInfo tone;
        if (deckMode_ == 2) {
            if (playDeckIndex_ < 0 || playDeckIndex_ >= (int) playDeck_.size()) return;
            tone = playDeck_[(size_t) playDeckIndex_];
        } else {
            const auto deck = favDeck();
            if (collectionIndex_ < 0 || collectionIndex_ >= (int) deck.size()) return;
            const auto& e = deck[(size_t) collectionIndex_];
            if (e.type == nam::LibraryType::Ir) { if (loadIr_) loadIr_ (e); return; }
            // Find the kept-tone info for this entry by display name (deck
            // may be filtered, so index mapping is unreliable).
            const auto kept = svc_.listKept ? svc_.listKept() : std::vector<nam::ToneInfo>{};
            bool found = false;
            for (const auto& k : kept)
                if (k.title == e.displayName) { tone = k; found = true; break; }
            if (! found) return;
        }
        if (! svc_.audition) return;
        // Hitting play means "I want to hear this": lift a user output mute.
        if (outMuted_ && muteOutput_) muteOutput_ (false);
        svc_.audition (tone, [this] (bool ok, juce::String) {
            auditioningPack_ = ok ? -2 : -1;
            play_->setDemoPlaying (ok);
        });
    };
    // --- Stacks: user-built rigs; every slot picks live from TONE3000 ----
    stacks_->onCreate = [this] {
        StacksScreen::Stack st;
        st.name = "STACK " + juce::String ((int) stackList_.size() + 1);
        stackList_.push_back (std::move (st));
        stackSel_ = (int) stackList_.size() - 1;
        saveStacksState();
        pushStacks();
    };
    stacks_->onDelete = [this] (int i) {
        if (i < 0 || i >= (int) stackList_.size()) return;
        stackList_.erase (stackList_.begin() + i);
        if (stackSel_ >= (int) stackList_.size()) stackSel_ = -1;
        saveStacksState();
        pushStacks();
    };
    stacks_->onSelect = [this] (int i) {
        stackSel_ = (stackSel_ == i ? -1 : i);
        pushStacks();
    };
    stacks_->onApply = [this] (int i) { applyStack (i); };
    stacks_->onFetchSlotOptions = [this] (int slot,
            std::function<void (juce::StringArray, juce::StringArray,
                                juce::StringArray)> cb) {
        if (! svc_.searchEx || slot < 0 || slot >= StacksScreen::kNumSlots) {
            cb ({}, {}, {});
            return;
        }
        // Trending list for the slot's gear type, fetched on demand — nothing
        // needs downloading up front.
        nam::SearchParams p;
        p.sort = "trending";
        const auto& def = StacksScreen::slotDefs()[(size_t) slot];
        if (juce::String (def.gearApi) == "ir") { p.format = "ir"; p.gears.push_back ("cab"); }
        else                                    p.gears.push_back (def.gearApi);
        svc_.searchEx (p, [cb] (bool ok, std::vector<nam::ToneInfo> tones, juce::String) {
            juce::StringArray ids, titles, formats;
            if (ok)
                for (const auto& t : tones) {
                    ids.add (juce::String (t.id));
                    titles.add (juce::String (t.title));
                    formats.add (juce::String (t.format));
                }
            cb (std::move (ids), std::move (titles), std::move (formats));
        });
    };
    stacks_->onSlotPicked = [this] (int stack, int slot, juce::String id,
                                    juce::String title, juce::String format) {
        if (stack < 0 || stack >= (int) stackList_.size()
            || slot < 0 || slot >= StacksScreen::kNumSlots)
            return;
        auto& st = stackList_[(size_t) stack];
        st.toneIds[(size_t) slot] = std::move (id);
        st.titles [(size_t) slot] = std::move (title);
        st.formats[(size_t) slot] = std::move (format);
        saveStacksState();
        pushStacks();
    };

    play_->onTuner    = [this] { toggleTuner(); };
    tuner_->onBack    = [this] { if (tunerOpen_) toggleTuner(); };
    addChildComponent (tunerScrim_);
    tunerScrim_.onTap = [this] { if (tunerOpen_) toggleTuner(); };
    addChildComponent (ioScrim_);
    ioScrim_.onTapAt  = [this] (juce::Point<int> p) { handleIoPanelTap (p); };
    ioScrim_.onDragAt = [this] (juce::Point<int> p) { handleIoDragAt (p); };
    ioScrim_.onUpAt   = [this] (juce::Point<int> p) { handleIoUpAt (p); };
    addChildComponent (moreScrim_);
    moreScrim_.onTapAt = [this] (juce::Point<int> p) {
        // Single row for now: DOWNLOADED (the deck moved out of the nav).
        if (moreOpen_ && moreRect_.reduced (6).contains (p)) {
            closeMoreMenu();
            setDeckMode (1);
            return;
        }
        closeMoreMenu();
    };
    devices_->onBack  = [this] { show (Screen::Play); };
    edit_->onDone    = [this] { show (Screen::Play); };
    browse_->onBack  = [this] { show (Screen::Play); };
    library_->onBack = [this] { show (Screen::Play); };
    live_->onExit    = [this] { show (Screen::Play); };

    devices_->onSetInputDb = [this] (float db) { engine_.setInputDb (db); };

    // Card-back quick sliders -> engine (same ranges as the EDIT screen).
    play_->onToneParam = [this] (int idx, float v) {
        switch (idx) {
            case 0: engine_.setInputDb (-24.0f + v * 48.0f); break;
            case 1: engine_.setLowDb  ((v - 0.5f) * 12.5f); break;
            case 2: engine_.setMidDb  ((v - 0.5f) * 12.5f); break;
            case 3: engine_.setHighDb ((v - 0.5f) * 12.5f); break;
            case 4: engine_.setGateThresholdDb (-70.0f + v * 50.0f); break;
            default: break;
        }
    };

    show (Screen::Play);
}

void AppShell::setBrowseServices (BrowseServices services) {
    svc_ = std::move (services);

    browse_->onQuery = [this] (juce::String q) { runBrowseSearch (std::move (q)); };

    browse_->onSort = [this] (int mode) {
        browseSort_ = mode;
        pushBrowseResults();
    };

    browse_->onFavorites = [this] {
        stopAudition();
        browseUnsorted_ = svc_.listKept ? svc_.listKept() : std::vector<nam::ToneInfo>{};
        pushBrowseResults();
        browse_->setStatus (browseUnsorted_.empty()
            ? ("Deck is empty " + kDotSep + " " + kHeart + " tones to collect them")
            : ("Your tone deck " + kDotSep + " "
               + juce::String ((int) browseUnsorted_.size())
               + (browseUnsorted_.size() == 1 ? " tone" : " tones")));
    };

    browse_->onKeep = [this] (int idx) {
        if (! svc_.keep || idx < 0 || idx >= (int) browseResults_.size()) return;
        const auto tone = browseResults_[(size_t) idx];
        const bool wasKept = svc_.isKept && svc_.isKept (tone.id);
        browse_->setStatus ((wasKept ? "Removing \"" : "Adding \"")
                            + juce::String (tone.title) + "\"" + kEllipsis);
        svc_.keep (tone, [this, wasKept] (bool ok, juce::String msg) {
            browse_->setStatus (! ok ? ("Deck update failed: " + msg)
                                : wasKept ? ("Removed from deck: " + msg)
                                          : (juce::String::fromUTF8 ("\xE2\x99\xA5") + " In deck: " + msg));
            refreshCachedFlags();
        });
    };

    browse_->onExpand = [this] (int idx) {
        if (! svc_.listModels || idx < 0 || idx >= (int) browseResults_.size()) return;
        const auto toneId = browseResults_[(size_t) idx].id;
        svc_.listModels (toneId,
            [this, idx, toneId] (bool ok, std::vector<nam::ModelInfo> models, juce::String err) {
                if (! ok) { browse_->setStatus ("Models: " + err); return; }
                // A newer search may have replaced the rows while this was in
                // flight — only bind if row idx is still the SAME tone.
                if (idx >= (int) browseModels_.size()
                    || idx >= (int) browseResults_.size()
                    || browseResults_[(size_t) idx].id != toneId)
                    return;
                browseModels_[(size_t) idx] = models;
                juce::StringArray names;
                for (const auto& m : models)
                    names.add (juce::String (m.name.empty() ? m.id : m.name));
                browse_->setModels (idx, names);
                // Tell the UI which variant "Auto (best)" actually resolves to.
                nam::ModelInfo best;
                if (nam::pickBestModel (models, best)) {
                    int bestIdx = -1;
                    for (size_t k = 0; k < models.size(); ++k)
                        if (models[k].id == best.id) { bestIdx = (int) k; break; }
                    browse_->setDefaultModel (idx, bestIdx,
                        juce::String (best.name.empty() ? best.id : best.name));
                }
            });
    };

    browse_->onPlayPack = [this] (int idx) {
        if (idx < 0 || idx >= (int) browseResults_.size()) return;
        if (idx == auditioningPack_ && auditioningModel_ < 0) {   // toggle stop
            stopAudition();
            browse_->setStatus ("Stopped");
            return;
        }
        if (! svc_.audition) return;
        const auto tone = browseResults_[(size_t) idx];
        browse_->setLoading (idx, 0.04f);
        browse_->setStatus ("Preparing \"" + juce::String (tone.title) + "\"" + kEllipsis);
        svc_.audition (tone, [this, idx, tone] (bool ok, juce::String msg) {
            browse_->setLoading (-1, 0.0f);
            if (! ok) { browse_->setStatus ("Audition failed: " + msg); return; }
            auditioningPack_ = idx;
            auditioningModel_ = -1;
            browse_->setPlaying (idx, -1);
            browse_->setStatus ("Auditioning \"" + juce::String (tone.title) + "\"");
            refreshCachedFlags();
            // The engine now runs this tone: reflect it on the Play screen.
            auditionToneId_ = tone.id;
            collectionIndex_ = -1;
            play_->setNowPlaying (juce::String (tone.title),
                                  (tone.gear.empty() ? juce::String ("TONE3000")
                                   : juce::String (tone.gear).toUpperCase() + " " + kDotSep + " TONE3000"),
                                  {});
            play_->setPosition (-1, 0);
        });
    };

    browse_->onPlayModel = [this] (int idx, int modelIdx) {
        if (! svc_.auditionModel || idx < 0 || idx >= (int) browseModels_.size()) return;
        const auto& models = browseModels_[(size_t) idx];
        if (modelIdx < 0 || modelIdx >= (int) models.size()) return;
        const auto tone = browseResults_[(size_t) idx];
        const auto model = models[(size_t) modelIdx];
        browse_->setLoading (idx, 0.04f);
        browse_->setStatus ("Preparing variant" + kEllipsis);
        svc_.auditionModel (tone.id, model, tone.format == "ir",
                            [this, idx, modelIdx, tone] (bool ok, juce::String msg) {
            browse_->setLoading (-1, 0.0f);
            if (! ok) { browse_->setStatus ("Audition failed: " + msg); return; }
            auditioningPack_ = idx;
            auditioningModel_ = modelIdx;
            browse_->setPlaying (idx, modelIdx);
            browse_->setStatus ("Auditioning \"" + msg + "\"");
            if (tone.format != "ir") auditionToneId_ = tone.id;   // IRs swap the cab, not the amp
            collectionIndex_ = -1;
            play_->setNowPlaying (msg, "TONE3000", {});
            play_->setPosition (-1, 0);
        });
    };

    browse_->onCab = [this] (int cabIdx) {
        if (svc_.setCab) svc_.setCab (cabIdx);
        refreshCachedFlags();
        // Live auditions pick the cab up instantly; re-render buffered ones.
        if (auditioningPack_ >= 0) {
            const int pack = auditioningPack_, model = auditioningModel_;
            if (model >= 0) browse_->onPlayModel (pack, model);
            else { auditioningPack_ = -1; browse_->onPlayPack (pack); }
        }
    };

    browse_->onDemoTrack = [this] (int track) {
        if (! svc_.setDemoTrack) return;
        browse_->setStatus ("Fetching demo track" + kEllipsis);
        svc_.setDemoTrack (track, [this] (bool ok) {
            if (! ok) { browse_->setStatus ("Demo track unavailable (offline?)"); return; }
            browse_->setStatus ("Demo track ready");
            refreshCachedFlags();
            // Re-audition with the new riff, if something is playing.
            if (auditioningPack_ >= 0) {
                const int pack = auditioningPack_, model = auditioningModel_;
                if (model >= 0) browse_->onPlayModel (pack, model);
                else { auditioningPack_ = -1; browse_->onPlayPack (pack); }
            }
        });
    };

    // Saved stacks live behind the host's persistence service.
    loadStacksState();
    pushStacks();
}

void AppShell::stopAudition() {
    if (svc_.stopDemo) svc_.stopDemo();
    auditioningPack_ = auditioningModel_ = -1;
    browse_->setPlaying (-1, -1);
    browse_->setLoading (-1, 0.0f);
    play_->setDemoPlaying (false);
}

void AppShell::setAuditionProgress (float progress) {
    browse_->setLoadingProgress (progress);
}

void AppShell::refreshCachedFlags() {
    if (svc_.isAuditionCached) {
        std::vector<bool> flags (browseResults_.size(), false);
        for (size_t i = 0; i < browseResults_.size(); ++i)
            flags[i] = svc_.isAuditionCached (browseResults_[i].id);
        browse_->setCachedFlags (std::move (flags));
    }
    if (svc_.isDownloaded) {
        std::vector<bool> dl (browseResults_.size(), false);
        for (size_t i = 0; i < browseResults_.size(); ++i)
            dl[i] = svc_.isDownloaded (browseResults_[i].id);
        browse_->setDownloadedFlags (std::move (dl));
    }
    if (svc_.isKept) {
        std::vector<bool> kept (browseResults_.size(), false);
        for (size_t i = 0; i < browseResults_.size(); ++i)
            kept[i] = svc_.isKept (browseResults_[i].id);
        browse_->setKeptFlags (std::move (kept));
    }
}

void AppShell::setLibraryService (GetModelsFn getModels, LoadModelFn loadModel) {
    getModels_ = std::move (getModels);
    loadModel_ = std::move (loadModel);
    library_->onLoad = [this] (nam::LibraryEntry e) {
        if (loadModel_) loadModel_ (e);
        showEntryAsNowPlaying (e);
        show (Screen::Play);
    };
    live_->onSelect  = [this] (nam::LibraryEntry e) {
        if (loadModel_) loadModel_ (e);
        showEntryAsNowPlaying (e);
    };
}

std::vector<nam::LibraryEntry> AppShell::favDeckAll() const {
    // Combined favorites deck: models + kept IRs, in the order they were
    // hearted (stable sort keeps store order for ties).
    std::vector<nam::LibraryEntry> deck;
    if (getModels_) for (auto& e : getModels_()) deck.push_back (e);
    if (getIrs_)    for (auto& e : getIrs_())    deck.push_back (e);
    std::stable_sort (deck.begin(), deck.end(),
                      [] (const auto& a, const auto& b) { return a.addedAt < b.addedAt; });
    return deck;
}

std::vector<nam::LibraryEntry> AppShell::favDeck() const {
    // Apply the local-deck filters: mode scope (favorites vs all saved),
    // gear by entry type; tags/makes by display-name match (OR within a
    // group, AND across groups).
    auto deck = favDeckAll();
    auto matches = [this] (const nam::LibraryEntry& e) {
        if (deckMode_ == 0 && ! e.favorite) return false;
        if (favGear_ == 0 && e.type != nam::LibraryType::Model) return false;
        if (favGear_ == 1 && e.type != nam::LibraryType::Ir) return false;
        const auto name = juce::String (e.displayName).toLowerCase();
        auto anyHit = [&name] (const juce::StringArray& terms) {
            for (const auto& t : terms)
                if (name.contains (t.toLowerCase())) return true;
            return false;
        };
        if (! favTags_.isEmpty() && ! anyHit (favTags_)) return false;
        if (! favMakes_.isEmpty() && ! anyHit (favMakes_)) return false;
        return true;
    };
    deck.erase (std::remove_if (deck.begin(), deck.end(),
                                [&] (const auto& e) { return ! matches (e); }),
                deck.end());
    return deck;
}

void AppShell::pushFilterGroups() {
    std::vector<PlayScreen::FilterGroup> groups;
    if (deckMode_ == 2) {
        // Full tone3000.com/search vocabulary via the real API parameters.
        // Gear lives in the strip dropdown, not the flyout.
        juce::StringArray gearChoices { "All Gear" };
        for (const auto& gd : kGearVocab) gearChoices.add (gd.display);
        play_->setGearChoices (std::move (gearChoices));
        PlayScreen::FilterGroup tags { "TAGS", {}, {}, false };
        for (const char* t : kTagVocab) tags.options.add (t);
        PlayScreen::FilterGroup makes { "MAKES & MODELS", {}, {}, false };
        for (const char* m : kMakeVocab) makes.options.add (m);
        PlayScreen::FilterGroup tech { "TECHNICAL", { "Any", "A2", "A1" }, { "Any" }, true };
        PlayScreen::FilterGroup sort { "SORT", {}, { "Trending" }, true };
        for (const auto& sd : kSortVocab) sort.options.add (sd.display);
        groups = { tags, makes, tech, sort };
        browseGroups_ = groups;
    } else {
        // Local decks: only offer chips the deck can actually satisfy (the
        // first word of a make is enough to match against display names).
        // Favorites mode derives from favorites only.
        auto deck = favDeckAll();
        if (deckMode_ == 0)
            deck.erase (std::remove_if (deck.begin(), deck.end(),
                                        [] (const auto& e) { return ! e.favorite; }),
                        deck.end());
        PlayScreen::FilterGroup tags { "TAGS", {}, {}, false };
        for (const char* t : kTagVocab)
            for (const auto& e : deck)
                if (juce::String (e.displayName).containsIgnoreCase (t)) {
                    tags.options.add (t);
                    break;
                }
        PlayScreen::FilterGroup makes { "MAKES & MODELS", {}, {}, false };
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
    const auto deck = favDeck();
    if (deck.empty()) {
        collectionIndex_ = -1;
        deckWindow_ = 0;
        play_->setNowPlaying ("Nothing kept yet", "NAM PLAYER", {});
        play_->setArtwork ({});
        play_->setKept (false);
        play_->setPosition (-1, 0);
        play_->setPageNav (false, false);
        pushDeckItems();
        play_->setActiveDeckIndex (-1);
        return;
    }
    const int n = (int) deck.size();
    index = ((index % n) + n) % n;
    collectionIndex_ = index;
    const auto& e = deck[(size_t) index];
    const bool isIr = (e.type == nam::LibraryType::Ir);
    if (loadIntoEngine) {
        if (isIr) { if (loadIr_) loadIr_ (e); }        // swap the cab, keep the amp
        else      { if (loadModel_) loadModel_ (e); }
    }
    juce::String family ("MY TONES");
    if (isIr) {
        family = "CABINET IR " + kDotSep + " MY TONES";
    } else if (! e.arch.empty()) {
        const auto a = juce::String (e.arch).toLowerCase();
        const bool isA2 = a.contains ("slim") || a.startsWith ("2") || a.startsWith ("a2");
        family = juce::String (isA2 ? "A2" : "A1") + " " + kDotSep + " MY TONES";
    }
    play_->setNowPlaying (juce::String (e.displayName), family, {});
    play_->setArtwork (artwork_ ? artwork_ (e) : juce::Image());
    play_->setKept (e.favorite);
    play_->setSaved (true);
    play_->setCabCard (isIr);
    if (curCardIsCab_ != isIr) {
        curCardIsCab_ = isIr;
        pushPairChoices();
    }
    // Dots show a 25-card window into big decks; ‹ › shifts the window.
    deckWindow_ = index / 25;
    const int wStart = deckWindow_ * 25;
    play_->setPosition (index - wStart, juce::jmin (25, n - wStart));
    play_->setPageNav (deckWindow_ > 0, wStart + 25 < n);
    pushDeckItems();
    play_->setActiveDeckIndex (index);
}

void AppShell::showBrowseCard (int index) {
    if (playDeck_.empty()) {
        playDeckIndex_ = -1;
        play_->setNowPlaying ("No results", "TONE3000", {});
        play_->setArtwork ({});
        play_->setKept (false);
        play_->setPosition (-1, 0);
        play_->setPageNav (browsePage_ > 1, false);   // back out of an empty page
        pushDeckItems();
        play_->setActiveDeckIndex (-1);
        return;
    }
    const int n = (int) playDeck_.size();
    index = ((index % n) + n) % n;
    playDeckIndex_ = index;
    const auto& t = playDeck_[(size_t) index];
    play_->setNowPlaying (juce::String (t.title),
                          (t.gear.empty() ? juce::String ("TONE3000")
                           : juce::String (t.gear).toUpperCase() + " " + kDotSep + " TONE3000"),
                          {});
    play_->setArtwork (svc_.artworkForTone ? svc_.artworkForTone (t) : juce::Image());
    play_->setKept (svc_.isKept && svc_.isKept (t.id));
    play_->setSaved (svc_.isSaved && svc_.isSaved (t.id));
    const bool cab = (t.format == "ir" || t.gear == "cab");
    play_->setCabCard (cab);
    if (curCardIsCab_ != cab) {
        curCardIsCab_ = cab;
        pushPairChoices();
    }
    play_->setPosition (index, n);
    // A full page implies more pages upstream (TONE3000 pages are 25).
    play_->setPageNav (browsePage_ > 1, n >= 25);
    pushDeckItems();
    play_->setActiveDeckIndex (index);
}

void AppShell::runPlayBrowse() {
    if (! svc_.searchEx) return;
    // Build real /tones/search params from the strip + flyout state.
    nam::SearchParams p;
    p.sort = "trending";
    p.page = browsePage_;
    const int gi = browseGear_ - 1;   // dropdown index 0 = All Gear
    if (gi >= 0 && gi < (int) (sizeof (kGearVocab) / sizeof (kGearVocab[0])))
        p.gears.push_back (kGearVocab[gi].api);
    for (const auto& gp : browseGroups_) {
        if (gp.title == "TAGS") {
            for (const auto& sel : gp.selected) p.tags.push_back (sel.toStdString());
        } else if (gp.title == "MAKES & MODELS") {
            for (const auto& sel : gp.selected) p.makes.push_back (sel.toStdString());
        } else if (gp.title == "TECHNICAL" && ! gp.selected.isEmpty()) {
            p.architecture = gp.selected[0] == "A2" ? 2 : gp.selected[0] == "A1" ? 1 : 0;
        } else if (gp.title == "SORT" && ! gp.selected.isEmpty()) {
            for (const auto& sd : kSortVocab)
                if (gp.selected[0] == sd.display) p.sort = sd.api;
        }
    }
    svc_.searchEx (p, [this] (bool ok, std::vector<nam::ToneInfo> tones, juce::String) {
        if (! ok) return;
        playDeck_ = std::move (tones);
        playDeckIndex_ = playDeck_.empty() ? -1 : 0;
        if (deckMode_ == 2) showBrowseCard (0);
    });
}

void AppShell::updateCabChoices() {
    cabChoiceNames_.clear();
    for (int c = 0; c < nam::demo::kNumCabs; ++c)
        cabChoiceNames_.add (nam::demo::kCabs[(size_t) c].display);
    cabBuiltinCount_ = cabChoiceNames_.size();
    if (getIrs_)
        for (const auto& e : getIrs_())
            cabChoiceNames_.add (juce::String (e.displayName));
    pushPairChoices();
}

std::vector<nam::LibraryEntry> AppShell::keptModelsSorted() const {
    auto ms = getModels_ ? getModels_() : std::vector<nam::LibraryEntry>{};
    std::stable_sort (ms.begin(), ms.end(),
                      [] (const auto& a, const auto& b) { return a.addedAt < b.addedAt; });
    return ms;
}

void AppShell::pushPairChoices() {
    if (curCardIsCab_) {
        // Cab card: choose which amp head drives it (TONE3000 cab pages do
        // the same). Options come from the kept/downloaded models.
        juce::StringArray names { "Current amp" };
        for (const auto& e : keptModelsSorted())
            names.add (juce::String (e.displayName));
        play_->setPairChoices ("PAIR AMP", std::move (names), 0);
    } else {
        play_->setPairChoices ("PAIR CAB", cabChoiceNames_, pairCabSel_);
    }
}

void AppShell::setDeckMode (int v) {
    if (current_ != play_.get()) show (Screen::Play);
    if (v == deckMode_) { repaint (navBar_); return; }
    deckMode_ = v;
    play_->setDeckView (v);
    favGear_ = -1;
    favTags_.clear();
    favMakes_.clear();
    browseGear_ = 0;
    browsePage_ = 1;
    deckWindow_ = 0;
    pushFilterGroups();
    if (svc_.stopDemo) svc_.stopDemo();
    play_->setDemoPlaying (false);
    if (deckMode_ == 2) runPlayBrowse();
    else                showFavCard (0, false);
    repaint (navBar_);
}

void AppShell::loadStacksState() {
    stackList_.clear();
    stackSel_ = -1;
    if (! svc_.loadStacksJson) return;
    const auto parsed = juce::JSON::parse (svc_.loadStacksJson());
    if (auto* arr = parsed.getArray())
        for (const auto& v : *arr) {
            auto* obj = v.getDynamicObject();
            if (obj == nullptr) continue;
            StacksScreen::Stack st;
            st.name = obj->getProperty ("name").toString();
            if (auto* slots = obj->getProperty ("slots").getArray())
                for (int k = 0; k < juce::jmin ((int) StacksScreen::kNumSlots,
                                                slots->size()); ++k) {
                    auto* so = (*slots)[k].getDynamicObject();
                    if (so == nullptr) continue;
                    st.toneIds[(size_t) k] = so->getProperty ("id").toString();
                    st.titles [(size_t) k] = so->getProperty ("title").toString();
                    st.formats[(size_t) k] = so->getProperty ("format").toString();
                }
            stackList_.push_back (std::move (st));
        }
}

void AppShell::saveStacksState() {
    if (! svc_.saveStacksJson) return;
    juce::Array<juce::var> arr;
    for (const auto& st : stackList_) {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("name", st.name);
        juce::Array<juce::var> slots;
        for (int k = 0; k < StacksScreen::kNumSlots; ++k) {
            auto* so = new juce::DynamicObject();
            so->setProperty ("id", st.toneIds[(size_t) k]);
            so->setProperty ("title", st.titles[(size_t) k]);
            so->setProperty ("format", st.formats[(size_t) k]);
            slots.add (juce::var (so));
        }
        obj->setProperty ("slots", slots);
        arr.add (juce::var (obj));
    }
    svc_.saveStacksJson (juce::JSON::toString (juce::var (arr)));
}

void AppShell::pushStacks() {
    if (stacks_ != nullptr) stacks_->setStacks (stackList_, stackSel_);
}

void AppShell::applyStack (int index) {
    if (index < 0 || index >= (int) stackList_.size() || ! svc_.loadTone) return;
    const auto& st = stackList_[(size_t) index];
    auto toneAt = [&st] (int k) {
        nam::ToneInfo t;
        t.id     = st.toneIds[(size_t) k].toStdString();
        t.title  = st.titles [(size_t) k].toStdString();
        t.format = st.formats[(size_t) k].toStdString();
        return t;
    };
    // The engine chain runs ONE model + ONE impulse: the first filled
    // head-ish slot becomes the model, the first filled cab/space slot the
    // impulse. Loads are SEQUENTIAL — the host funnels downloads through one
    // session thread, so a concurrent cab fetch would supersede (cancel) the
    // amp fetch and the amp would never load.
    int modelSlot = -1, irSlot = -1;
    for (int k : { 0, 2, 3, 5 })   // AMP, PEDAL, OUTBOARD, EXPERIMENTAL
        if (modelSlot < 0 && st.toneIds[(size_t) k].isNotEmpty()) modelSlot = k;
    for (int k : { 1, 4 })         // CABINET, SPACES
        if (irSlot < 0 && st.toneIds[(size_t) k].isNotEmpty()) irSlot = k;

    auto loadIrSlot = [this, irSlot, tone = irSlot >= 0 ? toneAt (irSlot)
                                                        : nam::ToneInfo{}] {
        if (irSlot >= 0) svc_.loadTone (tone, [] (bool, juce::String) {});
    };
    if (modelSlot >= 0) {
        setNowPlayingInfo (st.titles[(size_t) modelSlot],
                           st.name.toUpperCase() + " " + kDotSep + " STACK");
        svc_.loadTone (toneAt (modelSlot),
                       [loadIrSlot] (bool, juce::String) { loadIrSlot(); });
    } else {
        loadIrSlot();
    }
    stackSel_ = index;
    pushStacks();
}

void AppShell::showEntryAsNowPlaying (const nam::LibraryEntry& e) {
    auditionToneId_.clear();   // a library entry owns the engine now
    deckMode_ = 0;
    play_->setDeckView (0);
    const auto deck = favDeck();
    for (size_t i = 0; i < deck.size(); ++i)
        if (deck[i].id == e.id && deck[i].type == e.type) {
            showFavCard ((int) i, false);
            return;
        }
    collectionIndex_ = -1;
    play_->setNowPlaying (juce::String (e.displayName), "MY TONES", {});
    play_->setArtwork (artwork_ ? artwork_ (e) : juce::Image());
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

void AppShell::runBrowseSearch (juce::String q) {
    if (! svc_.search) return;
    stopAudition();
    browse_->setStatus (q.isEmpty() ? ("Tuning in to TONE3000" + kEllipsis)
                                    : ("Searching \"" + q + "\"" + kEllipsis));
    svc_.search (q, [this] (bool ok, std::vector<nam::ToneInfo> tones, juce::String err) {
        if (! ok) { browse_->setStatus ("TONE3000: " + err); return; }
        browseUnsorted_ = std::move (tones);
        pushBrowseResults();
        browse_->setStatus (juce::String ((int) browseUnsorted_.size())
                            + (browseUnsorted_.size() == 1 ? " pack" : " packs")
                            + " " + kDotSep + " tap a pack to expand " + kDotSep
                            + " " + kHeart + " keeps");
    });
}

void AppShell::pushBrowseResults() {
    // Sorting lives HERE so browseResults_ order always matches the rows the
    // screen displays (index-based callbacks depend on it).
    browseResults_ = browseUnsorted_;
    if (browseSort_ == 1)
        std::stable_sort (browseResults_.begin(), browseResults_.end(),
                          [] (const auto& a, const auto& b) { return a.downloads > b.downloads; });
    browseModels_.assign (browseResults_.size(), {});
    browse_->setResults (browseResults_);
    refreshCachedFlags();
}

void AppShell::setAudioDeviceService (GetDevicesFn get, SelectDeviceFn selectInput,
                                      SelectDeviceFn selectOutput, RescanFn rescan,
                                      SelectDeviceFn selectRate, SelectDeviceFn selectBuffer) {
    getDevices_     = std::move (get);
    selectInput_    = std::move (selectInput);
    selectOutput_   = std::move (selectOutput);
    rescanDevices_  = std::move (rescan);
    selectRate_     = std::move (selectRate);
    selectBuffer_   = std::move (selectBuffer);

    devices_->onRescan = [this] { if (rescanDevices_) rescanDevices_(); refreshDevices(); };
    devices_->onSelectInput = [this] (juce::String name) {
        if (selectInput_) selectInput_ (name);
        refreshDevices();
    };
    devices_->onSelectOutput = [this] (juce::String name) {
        if (selectOutput_) selectOutput_ (name);
        refreshDevices();
    };
    devices_->onSelectRate = [this] (juce::String label) {
        if (selectRate_) selectRate_ (label);
        refreshDevices();
    };
    devices_->onSelectBuffer = [this] (juce::String label) {
        if (selectBuffer_) selectBuffer_ (label);
        refreshDevices();
    };
}

void AppShell::refreshDevices() {
    if (! getDevices_) return;
    devices_->setState (getDevices_());
}

bool AppShell::handleBackButton() {
    if (tunerOpen_) { toggleTuner(); return true; }
    if (current_ != nullptr && current_ != play_.get()) { show (Screen::Play); return true; }
    return false;
}

void AppShell::toggleTuner() {
    if (play_ == nullptr || tuner_ == nullptr) return;
    auto& animator = juce::Desktop::getInstance().getAnimator();
    // Grows out of the Play tuner panel; the panel itself stays exposed
    // below the overlay as the collapse handle. Content-sized card, same
    // width/border language as the panel.
    const auto panel = play_->tunerPanelBounds() + play_->getPosition();
    const int h = juce::jmin (TunerScreen::kPanelHeight,
                              panel.getY() - play_->getY() - 16);
    const juce::Rectangle<int> expanded { panel.getX(), panel.getY() - 8 - h,
                                          panel.getWidth(), h };
    if (! tunerOpen_) {
        tunerOpen_ = true;
        animator.cancelAnimation (tuner_.get(), false);
        tuner_->setAlpha (1.0f);
        tuner_->setBounds (panel);
        tuner_->setVisible (true);
        tunerScrim_.setBounds (contentBounds());
        tunerScrim_.setVisible (true);
        tunerScrim_.toFront (false);
        tuner_->toFront (false);
        animator.animateComponent (tuner_.get(), expanded, 1.0f, 220, false, 1.0, 0.0);
    } else {
        tunerOpen_ = false;
        tunerScrim_.setVisible (false);
        animator.animateComponent (tuner_.get(), panel, 0.0f, 180, false, 1.0, 0.0);
        juce::Timer::callAfterDelay (200,
            [this, safe = juce::Component::SafePointer<TunerScreen> (tuner_.get())] {
                if (safe == nullptr || tunerOpen_) return;
                safe->setVisible (false);
                safe->setAlpha (1.0f);
            });
    }
}

void AppShell::show (Screen s) {
    closeIoPanel();
    closeMoreMenu();

    // The tuner overlay belongs to Play: navigating anywhere closes it.
    if (tunerOpen_ && s != Screen::Play) {
        juce::Desktop::getInstance().getAnimator().cancelAnimation (tuner_.get(), false);
        tuner_->setVisible (false);
        tuner_->setAlpha (1.0f);
        tunerScrim_.setVisible (false);
        tunerOpen_ = false;
    }

    // Refresh data-backed screens as they come into view.
    if (s == Screen::Library && getModels_) library_->setEntries (getModels_());
    if (s == Screen::Live && getModels_)     live_->setSlots (getModels_());
    if (s == Screen::Devices)                refreshDevices();
    if (s == Screen::Stacks)                 pushStacks();

    // Browse opens onto the live TONE3000 catalog (browse-by-default).
    if (s == Screen::Browse && ! browseLoadedOnce_ && svc_.search) {
        browseLoadedOnce_ = true;
        runBrowseSearch ({});
    }
    // Leaving Browse stops any audition demo.
    if (s != Screen::Browse && auditioningPack_ >= 0)
        stopAudition();

    // Arriving at Play with an audition tone still in the engine: if that
    // tone was hearted into the deck, promote it to the ACTIVE library entry
    // (real load — position, artwork, and boot persistence follow). If it
    // was never kept the engine keeps running it live; nothing to reload.
    if (s == Screen::Play && ! auditionToneId_.empty()) {
        const auto toneId = auditionToneId_;
        auditionToneId_.clear();
        if (getModels_ && loadModel_ && svc_.libraryIdForTone) {
            const auto libId = svc_.libraryIdForTone (toneId);
            if (! libId.empty())
                for (const auto& e : getModels_())
                    if (e.id == libId) {
                        loadModel_ (e);
                        showEntryAsNowPlaying (e);
                        break;
                    }
        }
    }

    juce::Component* target = play_.get();
    switch (s) {
        case Screen::Edit:    target = edit_.get();    break;
        case Screen::Browse:  target = browse_.get();  break;
        case Screen::Library: target = library_.get(); break;
        case Screen::Live:    target = live_.get();    break;
        case Screen::Devices: target = devices_.get(); break;
        case Screen::Stacks:  target = stacks_.get();  break;
        case Screen::Play:    default: target = play_.get(); break;
    }
    // Persistent chrome: which nav tab is lit for this screen.
    switch (s) {
        case Screen::Play:    activeTab_ = 0; break;
        case Screen::Edit:    activeTab_ = 1; break;
        case Screen::Browse:  activeTab_ = 2; break;
        case Screen::Live:    activeTab_ = 3; break;
        case Screen::Devices: activeTab_ = 4; break;
        default:              activeTab_ = -1; break;
    }
    repaint (navBar_);

    if (current_ == target) return;
    if (current_ != nullptr) current_->setVisible (false);
    current_ = target;
    current_->setBounds (contentBounds());
    current_->setVisible (true);
    current_->toFront (false);
}

void AppShell::setLevels (float in, float out) {
    if (play_ != nullptr)    play_->setLevels (in, out);
    if (devices_ != nullptr) devices_->setLevels (in, out);
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
        if (tunerMiss_ < 10 && ++tunerMiss_ >= 10)
            play_->setTuner ({}, 0.0f, false);
        if (tuner_ != nullptr) tuner_->setPitch (0.0f);
        return;
    }
    tunerMiss_ = 0;
    if (play_ == nullptr) return;
    if (tuner_ != nullptr) tuner_->setPitch (hz);
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F",
                                   "F#", "G", "G#", "A", "A#", "B" };
    const double midi = 69.0 + 12.0 * std::log2 ((double) hz / 440.0);
    const int nearest = (int) std::round (midi);
    const float cents = (float) ((midi - (double) nearest) * 100.0);
    const int nameIdx = ((nearest % 12) + 12) % 12;
    const int octave = nearest / 12 - 1;
    play_->setTuner (juce::String (names[nameIdx]) + juce::String (octave), cents, true);
}

juce::Rectangle<int> AppShell::contentBounds() const {
    auto b = getLocalBounds();
    b.removeFromBottom (navBar_.getHeight());
    return b;
}

void AppShell::resized() {
    auto b = getLocalBounds();
    // Bottom chrome: BROWSE / FAVORITES | status orb | DOWNLOADED / STACKS.
    navBar_ = b.removeFromBottom (juce::jmax (72, getHeight() / 12));
    for (auto& r : navRects_) r = {};
    const int orbD = juce::jmin (58, navBar_.getHeight() - 10);
    orbRect_ = { navBar_.getCentreX() - orbD / 2,
                 navBar_.getCentreY() - orbD / 2, orbD, orbD };
    {
        auto left  = navBar_.withTrimmedRight (navBar_.getWidth() / 2 + orbD / 2 + 6)
                            .withTrimmedLeft (8);
        auto right = navBar_.withTrimmedLeft (navBar_.getWidth() / 2 + orbD / 2 + 6)
                            .withTrimmedRight (8);
        navBrowseRect_ = left.removeFromLeft (left.getWidth() / 2);
        navFavRect_    = left;
        navStacksRect_ = right.removeFromLeft (right.getWidth() / 2);
        navMoreRect_   = right;
    }

    // I/O panel floats above the nav, centred on the orb: ENGINE row
    // (rate/buffer pickers), INPUT, OUTPUT, then the TEST TONE row.
    const int pw = juce::jmin (380, getWidth() - 48);
    ioPanelRect_ = { getWidth() / 2 - pw / 2, navBar_.getY() - 248 - 10, pw, 248 };
    auto rows = ioPanelRect_.reduced (14, 12);
    ioEngRow_ = rows.removeFromTop (52);
    rows.removeFromTop (8);
    ioInRow_  = rows.removeFromTop (52);
    rows.removeFromTop (8);
    ioOutRow_ = rows.removeFromTop (52);
    ioTestRect_ = rows.removeFromBottom (36).reduced (rows.getWidth() / 4, 0);
    {
        auto pills = ioEngRow_.reduced (14, 6).removeFromRight (150).withTrimmedTop (10);
        ioRatePill_ = pills.removeFromLeft (70).withSizeKeepingCentre (70, 28);
        ioBufPill_  = pills.removeFromRight (70).withSizeKeepingCentre (70, 28);
    }

    for (juce::Component* c : { (juce::Component*) play_.get(), (juce::Component*) edit_.get(),
                                (juce::Component*) browse_.get(), (juce::Component*) library_.get(),
                                (juce::Component*) live_.get(), (juce::Component*) devices_.get(),
                                (juce::Component*) stacks_.get() })
        if (c != nullptr) c->setBounds (b);

    // The tuner overlay tracks the Play tuner panel, not the screen grid.
    if (tuner_ != nullptr && tunerOpen_) {
        const auto panel = play_->tunerPanelBounds() + play_->getPosition();
        const int h = juce::jmin (TunerScreen::kPanelHeight,
                                  panel.getY() - play_->getY() - 16);
        tuner_->setBounds ({ panel.getX(), panel.getY() - 8 - h, panel.getWidth(), h });
        tunerScrim_.setBounds (contentBounds());
    }
}

void AppShell::paint (juce::Graphics& g) {
    // Global bottom chrome only — screens paint everything above it.
    g.setColour (nam::ui::col::bg);
    g.fillRect (navBar_);

    // Nav buttons flanking the orb: vector icon over a micro-label. Deck
    // buttons light up for the Play deck they select; STACKS for its screen.
    {
        const bool onPlay = (current_ == play_.get());
        auto navBtn = [&] (juce::Rectangle<int> r, const char* label, bool active,
                           int iconKind) {
            const auto c = active ? nam::ui::col::accent : nam::ui::col::inkA (0.45f);
            const auto ib = r.withSizeKeepingCentre (18, 18).toFloat()
                                .translated (0.0f, -8.0f);
            g.setColour (c);
            switch (iconKind) {
                case 0: {   // magnifier (browse)
                    const juce::Rectangle<float> lens (ib.getX() + 1.0f, ib.getY() + 1.0f,
                                                       11.0f, 11.0f);
                    g.drawEllipse (lens, 1.6f);
                    g.drawLine ({ { lens.getRight() - 1.5f, lens.getBottom() - 1.5f },
                                  { ib.getRight() - 1.0f, ib.getBottom() - 1.0f } }, 1.8f);
                    break;
                }
                case 1: {   // heart (favorites)
                    juce::Path p;
                    p.startNewSubPath (0.50f, 0.32f);
                    p.cubicTo (0.50f, 0.20f, 0.38f, 0.12f, 0.27f, 0.12f);
                    p.cubicTo (0.11f, 0.12f, 0.04f, 0.26f, 0.04f, 0.38f);
                    p.cubicTo (0.04f, 0.58f, 0.26f, 0.74f, 0.50f, 0.92f);
                    p.cubicTo (0.74f, 0.74f, 0.96f, 0.58f, 0.96f, 0.38f);
                    p.cubicTo (0.96f, 0.26f, 0.89f, 0.12f, 0.73f, 0.12f);
                    p.cubicTo (0.62f, 0.12f, 0.50f, 0.20f, 0.50f, 0.32f);
                    p.closeSubPath();
                    p.applyTransform (juce::AffineTransform::scale (ib.getWidth(), ib.getHeight())
                                          .translated (ib.getX(), ib.getY()));
                    if (active) g.fillPath (p);
                    else        g.strokePath (p, juce::PathStrokeType (1.5f));
                    break;
                }
                case 2: {   // down arrow into tray (downloaded)
                    const float cx = ib.getCentreX();
                    g.drawLine ({ { cx, ib.getY() + 1.0f }, { cx, ib.getBottom() - 6.0f } }, 1.8f);
                    juce::Path a;
                    a.addTriangle (cx - 4.5f, ib.getBottom() - 8.5f,
                                   cx + 4.5f, ib.getBottom() - 8.5f,
                                   cx,        ib.getBottom() - 3.5f);
                    g.fillPath (a);
                    g.fillRoundedRectangle (ib.getX(), ib.getBottom() - 1.5f,
                                            ib.getWidth(), 1.8f, 0.9f);
                    break;
                }
                case 3: {   // three stacked bars (stacks)
                    for (int i = 0; i < 3; ++i)
                        g.fillRoundedRectangle (ib.getX() + (float) (2 - i) * 1.5f,
                                                ib.getY() + 1.0f + (float) i * 5.5f,
                                                ib.getWidth() - (float) (2 - i) * 3.0f,
                                                3.0f, 1.5f);
                    break;
                }
                default: {  // horizontal three dots (more)
                    for (int i = 0; i < 3; ++i)
                        g.fillEllipse (ib.getX() + (float) i * 6.5f,
                                       ib.getCentreY() - 1.75f, 3.5f, 3.5f);
                    break;
                }
            }
            g.setFont (nam::ui::uiFontTracked (8.0f, true));
            g.setColour (c);
            g.drawText (label, r.withTrimmedTop (r.getHeight() / 2 + 6),
                        juce::Justification::centredTop, false);
        };
        navBtn (navBrowseRect_, "BROWSE",  onPlay && deckMode_ == 2, 0);
        navBtn (navFavRect_,    "FAVORITES", onPlay && deckMode_ == 0, 1);
        navBtn (navStacksRect_, "STACKS",  current_ == stacks_.get(), 3);
        navBtn (navMoreRect_,   "MORE", moreOpen_ || (onPlay && deckMode_ == 1), 4);
    }

    // Status orb: input level = left arc, output level = right arc, both
    // filling upward from 6 o'clock. Centre shows the round-trip latency
    // (green -> red as it degrades). A muted side's track turns red.
    {
        const auto ob = orbRect_.toFloat().reduced (3.0f);
        const auto cx = ob.getCentreX(), cy = ob.getCentreY();
        const float r = ob.getWidth() * 0.5f;
        constexpr float pi = juce::MathConstants<float>::pi;

        auto levelFrac = [] (float peak) {
            const float db = peak > 0.001f ? 20.0f * std::log10 (peak) : -60.0f;
            return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        };
        auto arc = [&] (float fromRad, float toRad, juce::Colour c, float thickness) {
            juce::Path p;
            p.addCentredArc (cx, cy, r, r, 0.0f, fromRad, toRad, true);
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (thickness,
                              juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };

        // Tracks (6 o'clock up each side). Red = that side is muted.
        arc (pi, 2.0f * pi, inMuted_ ? juce::Colour (0x55ff3b30) : nam::ui::col::inkA (0.10f), 3.0f);
        arc (pi, 0.0f,      outMuted_ ? juce::Colour (0x55ff3b30) : nam::ui::col::inkA (0.10f), 3.0f);

        // Levels: input grows both ways out of 9 o'clock, output out of
        // 3 o'clock (full level = that side's whole semicircle).
        if (! inMuted_) {
            const float f = levelFrac (meterInPeak_);
            if (f > 0.01f)
                arc (1.5f * pi - f * 0.5f * pi, 1.5f * pi + f * 0.5f * pi,
                     nam::ui::col::meterLime, 3.5f);
        }
        if (! outMuted_) {
            const float f = levelFrac (meterOutPeak_);
            if (f > 0.01f)
                arc (0.5f * pi - f * 0.5f * pi, 0.5f * pi + f * 0.5f * pi,
                     nam::ui::col::meterBlue, 3.5f);   // output = blue (vs lime input)
        }

        // Centre: latency ms over a "LATENCY" micro-label, or MUTE when the
        // output is silenced.
        const float bad = juce::jlimit (0.0f, 1.0f, ((float) latencyMs_ - 20.0f) / 30.0f);
        const auto readout = outMuted_ ? juce::Colour (0xffff3b30)
                                       : nam::ui::col::meterLime.interpolatedWith (
                                             juce::Colour (0xffff3b30), bad);
        if (outMuted_) {
            g.setFont (nam::ui::uiFont (9.0f, true));
            g.setColour (readout);
            g.drawText ("MUTE", orbRect_, juce::Justification::centred, false);
        } else {
            g.setFont (nam::ui::uiFont (10.0f, true));
            g.setColour (readout);
            g.drawText (latencyMs_ > 0.0 ? juce::String ((int) std::round (latencyMs_))
                                         : juce::String ("--"),
                        orbRect_.withTrimmedBottom (10), juce::Justification::centred, false);
            g.setFont (nam::ui::uiFontTracked (5.5f, true));
            g.setColour (nam::ui::col::inkA (0.45f));
            g.drawText ("LATENCY",
                        orbRect_.withTrimmedTop (orbRect_.getHeight() / 2 + 2),
                        juce::Justification::centredTop, false);
        }
    }

}

void AppShell::paintOverChildren (juce::Graphics& g) {
    // I/O device picker (replaces the panel while choosing a device).
    if (ioPanelOpen_ && ioPicker_ != 0) {
        auto shortName = [] (juce::String n) {
            return n.replace ("USB-Audio - ", "").replace (" USB headset", "").trim();
        };
        g.setColour (juce::Colour (0xf214101f));
        g.fillRoundedRectangle (ioPickerRect_.toFloat(), 14.0f);
        g.setColour (nam::ui::col::inkA (0.18f));
        g.drawRoundedRectangle (ioPickerRect_.toFloat().reduced (0.5f), 14.0f, 1.0f);
        g.setFont (nam::ui::uiFontTracked (9.0f, true));
        g.setColour (nam::ui::col::inkA (0.4f));
        g.drawText (ioPicker_ == 2 ? "OUTPUT DEVICE" : ioPicker_ == 1 ? "INPUT DEVICE"
                    : ioPicker_ == 3 ? "SAMPLE RATE" : "BUFFER SIZE",
                    ioPickerRect_.getX() + 18, ioPickerRect_.getY() + 10,
                    ioPickerRect_.getWidth() - 36, 16, juce::Justification::centredLeft, false);
        g.saveState();
        juce::Path clip;
        clip.addRoundedRectangle (ioPickerRect_.toFloat().reduced (1.0f)
                                      .withTrimmedTop (30.0f), 13.0f);
        g.reduceClipRegion (clip);
        constexpr int rowH = 44, titleH = 34, pad = 8;
        for (int i = 0; i < ioPickerItems_.size(); ++i) {
            const juce::Rectangle<int> row (ioPickerRect_.getX() + 6,
                ioPickerRect_.getY() + titleH + pad + i * rowH - (int) ioPickerScroll_,
                ioPickerRect_.getWidth() - 12, rowH);
            if (row.getBottom() < ioPickerRect_.getY() || row.getY() > ioPickerRect_.getBottom())
                continue;
            const bool sel = ioPickerItems_[i] == ioPickerCurrent_;
            if (sel) {
                g.setColour (nam::ui::col::accentA (0.10f));
                g.fillRoundedRectangle (row.toFloat(), 9.0f);
            }
            auto in = row.reduced (12, 0);
            g.setFont (nam::ui::uiFont (12.0f, false));
            g.setColour (nam::ui::col::accentAlt);
            g.drawText (sel ? juce::String::fromUTF8 ("\xE2\x9C\x93") : juce::String(),
                        in.removeFromLeft (18), juce::Justification::centredLeft, false);
            g.setFont (nam::ui::uiFont (13.0f, sel));
            g.setColour (sel ? nam::ui::col::accentAlt : nam::ui::col::inkA (0.85f));
            g.drawText (shortName (ioPickerItems_[i]), in, juce::Justification::centredLeft, false);
        }
        g.restoreState();
        if (ioPickerContentH_ > ioPickerRect_.getHeight()) {
            const float frac = (float) ioPickerRect_.getHeight() / (float) ioPickerContentH_;
            const float thumbH = juce::jmax (24.0f, ioPickerRect_.getHeight() * frac);
            const float travel = (float) ioPickerRect_.getHeight() - thumbH - 8.0f;
            const float pos = ioPickerScroll_
                            / (float) (ioPickerContentH_ - ioPickerRect_.getHeight());
            g.setColour (nam::ui::col::inkA (0.25f));
            g.fillRoundedRectangle ((float) ioPickerRect_.getRight() - 7.0f,
                                    (float) ioPickerRect_.getY() + 4.0f + travel * pos,
                                    3.0f, thumbH, 1.5f);
        }
        return;
    }

    // I/O mute panel (over the current screen, anchored to the orb).
    if (ioPanelOpen_) {
        g.setColour (nam::ui::col::bg);
        g.fillRoundedRectangle (ioPanelRect_.toFloat(), 14.0f);
        g.setColour (nam::ui::col::inkA (0.03f));
        g.fillRoundedRectangle (ioPanelRect_.toFloat(), 14.0f);
        g.setColour (nam::ui::col::inkA (0.16f));
        g.drawRoundedRectangle (ioPanelRect_.toFloat().reduced (0.5f), 14.0f, 1.0f);

        auto shortName = [] (juce::String n) {
            return n.replace ("USB-Audio - ", "").replace (" USB headset", "")
                    .replace ("System Default (Input)", "System Default")
                    .replace ("System Default (Output)", "System Default").trim();
        };
        juce::String inName ("--"), outName ("--"), rate ("--"), buffer ("--");
        if (getDevices_) {
            const auto st = getDevices_();
            inName = shortName (st.currentInput);
            outName = shortName (st.currentOutput);
            rate = st.currentRate.isNotEmpty() ? st.currentRate : juce::String ("--");
            buffer = st.currentBuffer.isNotEmpty() ? st.currentBuffer : juce::String ("--");
        }

        // ENGINE row: sample rate + buffer as tap-to-pick pills.
        {
            g.setColour (nam::ui::col::inkA (0.04f));
            g.fillRoundedRectangle (ioEngRow_.toFloat(), 10.0f);
            auto inner = ioEngRow_.reduced (14, 6);
            g.setFont (nam::ui::uiFontTracked (10.0f, true));
            g.setColour (nam::ui::col::inkA (0.45f));
            g.drawText ("ENGINE", inner.removeFromTop (inner.getHeight() / 2),
                        juce::Justification::bottomLeft, false);
            g.setFont (nam::ui::uiFont (11.0f, false));
            g.setColour (nam::ui::col::inkA (0.55f));
            g.drawText (latencyMs_ > 0.0
                            ? juce::String ((int) std::round (latencyMs_)) + " ms round trip"
                            : juce::String(),
                        inner, juce::Justification::topLeft, false);
            auto enginePill = [&] (juce::Rectangle<int> rr, const juce::String& value,
                                   const char* caption) {
                nam::ui::drawPill (g, rr.toFloat(), juce::Colours::transparentBlack,
                                   nam::ui::col::inkA (0.2f));
                auto in = rr.reduced (10, 0);
                g.setFont (nam::ui::uiFont (10.0f, false));
                g.setColour (nam::ui::col::inkA (0.5f));
                g.drawText (juce::String::fromUTF8 ("\xE2\x96\xBE"),
                            in.removeFromRight (12), juce::Justification::centred, false);
                g.setFont (nam::ui::uiFont (12.0f, true));
                g.setColour (nam::ui::col::ink);
                g.drawText (value, in, juce::Justification::centred, false);
                g.setFont (nam::ui::uiFontTracked (7.0f, true));
                g.setColour (nam::ui::col::inkA (0.4f));
                g.drawText (juce::String (caption),
                            juce::Rectangle<int> (rr.getX(), rr.getY() - 12, rr.getWidth(), 10),
                            juce::Justification::centred, false);
            };
            enginePill (ioRatePill_, rate, "RATE");
            enginePill (ioBufPill_, buffer, "BUFFER");
        }

        auto row = [&] (juce::Rectangle<int> rr, const juce::String& label,
                        const juce::String& device, bool muted, float level,
                        juce::Colour levelColour) {
            g.setColour (nam::ui::col::inkA (muted ? 0.02f : 0.04f));
            g.fillRoundedRectangle (rr.toFloat(), 10.0f);
            auto inner = rr.reduced (14, 6);
            auto toggle = inner.removeFromRight (74);
            auto meter = inner.removeFromRight (juce::jmax (60, inner.getWidth() / 3))
                             .reduced (10, 0);
            g.setFont (nam::ui::uiFontTracked (10.0f, true));
            g.setColour (nam::ui::col::inkA (0.45f));
            g.drawText (label, inner.removeFromTop (inner.getHeight() / 2),
                        juce::Justification::bottomLeft, false);
            g.setFont (nam::ui::uiFont (13.0f, true));
            g.setColour (muted ? nam::ui::col::inkA (0.4f) : nam::ui::col::ink);
            g.drawText (device, inner, juce::Justification::topLeft, false);
            // Horizontal level meter (Hi-Fi design), dead when muted.
            {
                const auto track = meter.withSizeKeepingCentre (meter.getWidth(), 5).toFloat();
                g.setColour (nam::ui::col::inkA (0.10f));
                g.fillRoundedRectangle (track, 2.5f);
                const float db = level > 0.001f ? 20.0f * std::log10 (level) : -60.0f;
                const float f = muted ? 0.0f
                              : juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
                if (f > 0.01f) {
                    g.setColour (levelColour);
                    g.fillRoundedRectangle (track.withWidth (
                        juce::jmax (4.0f, track.getWidth() * f)), 2.5f);
                }
            }
            nam::ui::drawPill (g, toggle.toFloat(),
                               muted ? juce::Colour (0x33ff3b30) : juce::Colours::transparentBlack,
                               muted ? juce::Colour (0xaaff3b30) : nam::ui::col::inkA (0.2f));
            g.setFont (nam::ui::uiFontTracked (10.0f, true));
            g.setColour (muted ? juce::Colour (0xffff3b30) : nam::ui::col::inkA (0.6f));
            g.drawText (muted ? "MUTED" : "MUTE", toggle, juce::Justification::centred, false);
        };
        row (ioInRow_,  "INPUT",  inName,  inMuted_,  meterInPeak_,  nam::ui::col::meterLime);
        row (ioOutRow_, "OUTPUT", outName, outMuted_, meterOutPeak_, nam::ui::col::meterBlue);

        // TEST TONE: plays a reference sine through the output path.
        nam::ui::drawPill (g, ioTestRect_.toFloat(),
                           testToneOn_ ? nam::ui::col::accentA (0.16f)
                                       : juce::Colours::transparentBlack,
                           testToneOn_ ? nam::ui::col::accentA (0.8f)
                                       : nam::ui::col::inkA (0.2f));
        g.setFont (nam::ui::uiFontTracked (10.0f, true));
        g.setColour (testToneOn_ ? nam::ui::col::accent : nam::ui::col::inkA (0.6f));
        g.drawText (testToneOn_ ? "STOP TONE" : "TEST TONE", ioTestRect_,
                    juce::Justification::centred, false);
    }

    // ⋯ menu (right corner, above the nav) — painted last (overlay rule).
    if (moreOpen_) {
        g.setColour (juce::Colour (0xf214101f));
        g.fillRoundedRectangle (moreRect_.toFloat(), 14.0f);
        g.setColour (nam::ui::col::inkA (0.18f));
        g.drawRoundedRectangle (moreRect_.toFloat().reduced (0.5f), 14.0f, 1.0f);
        auto rrow = moreRect_.reduced (6);
        const bool active = (current_ == play_.get() && deckMode_ == 1);
        if (active) {
            g.setColour (nam::ui::col::accentA (0.10f));
            g.fillRoundedRectangle (rrow.toFloat(), 9.0f);
        }
        // Down-arrow-into-tray icon + label (DOWNLOADED lives here now).
        auto in = rrow.reduced (14, 10);
        const auto ib = in.removeFromLeft (18).toFloat()
                            .withSizeKeepingCentre (16.0f, 16.0f);
        g.setColour (active ? nam::ui::col::accent : nam::ui::col::inkA (0.7f));
        const float cx = ib.getCentreX();
        g.drawLine ({ { cx, ib.getY() + 1.0f }, { cx, ib.getBottom() - 6.0f } }, 1.8f);
        juce::Path a;
        a.addTriangle (cx - 4.0f, ib.getBottom() - 8.0f, cx + 4.0f, ib.getBottom() - 8.0f,
                       cx, ib.getBottom() - 3.5f);
        g.fillPath (a);
        g.fillRoundedRectangle (ib.getX(), ib.getBottom() - 1.5f, ib.getWidth(), 1.8f, 0.9f);
        g.setFont (nam::ui::uiFont (13.0f, true));
        g.setColour (active ? nam::ui::col::accent : nam::ui::col::inkA (0.85f));
        g.drawText ("Downloaded", in.withTrimmedLeft (12),
                    juce::Justification::centredLeft, false);
    }
}

void AppShell::closeIoPanel() {
    if (! ioPanelOpen_) return;
    ioPanelOpen_ = false;
    ioPicker_ = 0;
    if (testToneOn_) {   // never leave the check tone running unattended
        testToneOn_ = false;
        if (svc_.setTestTone) svc_.setTestTone (false);
    }
    ioScrim_.setVisible (false);
    repaint();
}

void AppShell::openMoreMenu() {
    // Content-sized, right-anchored above the nav (house overlay style).
    constexpr int rowH = 46, w = 210;
    moreRect_ = { getWidth() - w - 12, navBar_.getY() - rowH - 20, w, rowH + 12 };
    moreOpen_ = true;
    moreScrim_.setBounds (contentBounds());
    moreScrim_.setVisible (true);
    moreScrim_.toFront (false);
    repaint();
}

void AppShell::closeMoreMenu() {
    if (! moreOpen_) return;
    moreOpen_ = false;
    moreScrim_.setVisible (false);
    repaint();
}

void AppShell::openIoPicker (bool output) {
    if (! getDevices_) return;
    const auto st = getDevices_();
    ioPickerItems_ = output ? st.outputs : st.inputs;
    ioPickerCurrent_ = output ? st.currentOutput : st.currentInput;
    ioPicker_ = output ? 2 : 1;
    ioPickerScroll_ = 0.0f;
    constexpr int rowH = 44, titleH = 34, pad = 8;
    ioPickerContentH_ = titleH + pad + rowH * ioPickerItems_.size() + pad;
    const int pw = juce::jmin (380, getWidth() - 48);
    // Height-capped (overlay rule): never more than ~55% of the content.
    const int maxH = contentBounds().getHeight() * 55 / 100;
    const int h = juce::jmin (ioPickerContentH_, maxH);
    ioPickerRect_ = { getWidth() / 2 - pw / 2, navBar_.getY() - h - 10, pw, h };
    repaint();
}

void AppShell::openEnginePicker (bool buffer) {
    if (! getDevices_) return;
    const auto st = getDevices_();
    ioPickerItems_ = buffer ? st.buffers : st.rates;
    ioPickerCurrent_ = buffer ? st.currentBuffer : st.currentRate;
    ioPicker_ = buffer ? 4 : 3;
    ioPickerScroll_ = 0.0f;
    constexpr int rowH = 44, titleH = 34, pad = 8;
    ioPickerContentH_ = titleH + pad + rowH * ioPickerItems_.size() + pad;
    const int pw = juce::jmin (380, getWidth() - 48);
    const int maxH = contentBounds().getHeight() * 55 / 100;   // overlay rule
    const int h = juce::jmin (ioPickerContentH_, maxH);
    ioPickerRect_ = { getWidth() / 2 - pw / 2, navBar_.getY() - h - 10, pw, h };
    repaint();
}

void AppShell::handleIoPanelTap (juce::Point<int> p) {
    if (ioPicker_ != 0) {
        if (ioPickerRect_.contains (p)) {
            ioPickerPressed_ = true;
            ioPickerMoved_ = false;
            ioPickerPressPos_ = p;
            ioPickerPressScroll_ = ioPickerScroll_;
        } else {
            ioPicker_ = 0;   // back to the mute panel
            repaint();
        }
        return;
    }
    // Main panel: ENGINE pills open the rate/buffer pickers; the toggle
    // pill mutes; the device area opens the device picker.
    if (ioRatePill_.expanded (4).contains (p)) { openEnginePicker (false); return; }
    if (ioBufPill_.expanded (4).contains (p))  { openEnginePicker (true);  return; }
    if (ioTestRect_.expanded (6).contains (p)) {
        testToneOn_ = ! testToneOn_;
        if (testToneOn_ && outMuted_ && muteOutput_) {   // hearing it is the point
            outMuted_ = false;
            muteOutput_ (false);
        }
        if (svc_.setTestTone) svc_.setTestTone (testToneOn_);
        repaint (ioPanelRect_.expanded (4));
        return;
    }
    if (ioEngRow_.contains (p)) return;   // row body: nothing to toggle
    if (ioInRow_.contains (p)) {
        if (p.x > ioInRow_.getRight() - 96) {
            inMuted_ = ! inMuted_;
            if (muteInput_) muteInput_ (inMuted_);
            repaint (ioPanelRect_); repaint (orbRect_.expanded (4));
        } else {
            openIoPicker (false);
        }
        return;
    }
    if (ioOutRow_.contains (p)) {
        if (p.x > ioOutRow_.getRight() - 96) {
            outMuted_ = ! outMuted_;
            if (muteOutput_) muteOutput_ (outMuted_);
            repaint (ioPanelRect_); repaint (orbRect_.expanded (4));
        } else {
            openIoPicker (true);
        }
        return;
    }
    closeIoPanel();
}

void AppShell::handleIoDragAt (juce::Point<int> p) {
    if (! ioPickerPressed_) return;
    const int dy = p.y - ioPickerPressPos_.y;
    if (std::abs (dy) > 8) ioPickerMoved_ = true;
    if (ioPickerMoved_) {
        ioPickerScroll_ = juce::jlimit (0.0f,
            (float) juce::jmax (0, ioPickerContentH_ - ioPickerRect_.getHeight()),
            ioPickerPressScroll_ - (float) dy);
        repaint (ioPickerRect_.expanded (2));
    }
}

void AppShell::handleIoUpAt (juce::Point<int> p) {
    if (! ioPickerPressed_) return;
    if (! ioPickerMoved_) {
        constexpr int rowH = 44, titleH = 34, pad = 8;
        const int i = (p.y - ioPickerRect_.getY() - titleH - pad + (int) ioPickerScroll_) / rowH;
        if (i >= 0 && i < ioPickerItems_.size()) {
            const auto name = ioPickerItems_[i];
            switch (ioPicker_) {
                case 2:  if (selectOutput_) selectOutput_ (name); break;
                case 1:  if (selectInput_)  selectInput_ (name);  break;
                case 3:  if (selectRate_)   selectRate_ (name);   break;
                default: if (selectBuffer_) selectBuffer_ (name); break;
            }
            ioPicker_ = 0;   // back to the panel with the new pick shown
            repaint();
        }
    }
    ioPickerPressed_ = ioPickerMoved_ = false;
}

void AppShell::mouseDown (const juce::MouseEvent& e) {
    const auto p = e.getPosition();

    if (ioPanelOpen_) {   // taps on the nav while the panel is up just close it
        closeIoPanel();
        return;
    }
    if (moreOpen_) { closeMoreMenu(); return; }

    if (orbRect_.expanded (6).contains (p)) {
        ioPanelOpen_ = true;
        ioScrim_.setBounds (contentBounds());
        ioScrim_.setVisible (true);
        ioScrim_.toFront (false);
        repaint();
        return;
    }

    // Nav buttons: deck pickers left of the orb; STACKS + ⋯ menu right.
    if (navBrowseRect_.contains (p)) { setDeckMode (2); return; }
    if (navFavRect_.contains (p))    { setDeckMode (0); return; }
    if (navStacksRect_.contains (p)) {
        show (Screen::Stacks);
        repaint (navBar_);
        return;
    }
    if (navMoreRect_.contains (p)) { openMoreMenu(); return; }

    // Tapping the bar anywhere else returns home to Play.
    if (navBar_.contains (p) && current_ != play_.get()) show (Screen::Play);
}
