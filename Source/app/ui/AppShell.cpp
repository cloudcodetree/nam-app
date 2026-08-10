#include "app/ui/AppShell.h"
#include "app/ui/DemoTrackCatalog.h"
#include "app/ui/NamLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace {
const juce::String kEllipsis = juce::String::fromUTF8 ("\xE2\x80\xA6"); // …
const juce::String kHeart    = juce::String::fromUTF8 ("\xE2\x99\xA5"); // ♥
const juce::String kDotSep   = juce::String::fromUTF8 ("\xC2\xB7");     // ·
}

AppShell::AppShell (dsp::ToneEngine& engine) : engine_ (engine) {
    play_    = std::make_unique<PlayScreen>();
    edit_    = std::make_unique<EditScreen> (engine_);
    browse_  = std::make_unique<BrowseScreen>();
    library_ = std::make_unique<LibraryScreen>();
    live_    = std::make_unique<LiveScreen>();
    devices_ = std::make_unique<AudioSettingsScreen>();
    tuner_   = std::make_unique<TunerScreen>();
    tuner_->setPanelMode (true);   // hosted as a card over Play, not a screen

    for (juce::Component* c : { (juce::Component*) play_.get(), (juce::Component*) edit_.get(),
                                (juce::Component*) browse_.get(), (juce::Component*) library_.get(),
                                (juce::Component*) live_.get(), (juce::Component*) devices_.get(),
                                (juce::Component*) tuner_.get() })
        addChildComponent (*c);

    play_->onLibrary  = [this] { show (Screen::Library); };
    play_->onSettings = [this] { show (Screen::Devices); };
    play_->onEdit     = [this] { show (Screen::Edit); };
    play_->onLive     = [this] { show (Screen::Live); };
    play_->onPrev     = [this] { stepCollection (-1); };
    play_->onNext     = [this] { stepCollection (+1); };
    play_->onSelectIndex = [this] (int k) {
        if (browseView_) showBrowseCard (k);
        else             showFavCard (k, true);
    };

    play_->onViewChange = [this] (int v) {
        const bool browse = (v == 1);
        if (browse == browseView_) return;
        browseView_ = browse;
        play_->setDeckView (v);
        if (svc_.stopDemo) svc_.stopDemo();
        play_->setDemoPlaying (false);
        if (browse) {
            if (playDeck_.empty()) runPlayBrowse ({});
            else showBrowseCard (playDeckIndex_ < 0 ? 0 : playDeckIndex_);
        } else {
            showFavCard (collectionIndex_ < 0 ? 0 : collectionIndex_, false);
        }
    };

    play_->onBrowseQuery = [this] (juce::String q) { runPlayBrowse (std::move (q)); };

    play_->onKeepToggle = [this] {
        if (browseView_) {
            if (playDeckIndex_ < 0 || playDeckIndex_ >= (int) playDeck_.size()
                || ! svc_.keep)
                return;
            const auto tone = playDeck_[(size_t) playDeckIndex_];
            svc_.keep (tone, [this, tone] (bool ok, juce::String) {
                if (! ok) return;
                play_->setKept (svc_.isKept && svc_.isKept (tone.id));
                updateCabChoices();
            });
        } else {
            const auto deck = favDeck();
            if (collectionIndex_ < 0 || collectionIndex_ >= (int) deck.size()
                || ! svc_.removeKept)
                return;
            svc_.removeKept (deck[(size_t) collectionIndex_].id);
            updateCabChoices();
            showFavCard (collectionIndex_, true);   // next card slides into the slot
        }
    };

    play_->onSelectCab = [this] (int i) {
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
            return;
        }
        nam::ToneInfo tone;
        if (browseView_) {
            if (playDeckIndex_ < 0 || playDeckIndex_ >= (int) playDeck_.size()) return;
            tone = playDeck_[(size_t) playDeckIndex_];
        } else {
            const auto deck = favDeck();
            if (collectionIndex_ < 0 || collectionIndex_ >= (int) deck.size()) return;
            const auto& e = deck[(size_t) collectionIndex_];
            if (e.type == nam::LibraryType::Ir) { if (loadIr_) loadIr_ (e); return; }
            int mi = 0;   // model position -> listKept index (same addedAt order)
            for (int k = 0; k < collectionIndex_; ++k)
                if (deck[(size_t) k].type == nam::LibraryType::Model) ++mi;
            const auto kept = svc_.listKept ? svc_.listKept() : std::vector<nam::ToneInfo>{};
            if (mi >= (int) kept.size()) return;
            tone = kept[(size_t) mi];
        }
        if (! svc_.audition) return;
        svc_.audition (tone, [this] (bool ok, juce::String) {
            auditioningPack_ = ok ? -2 : -1;
            play_->setDemoPlaying (ok);
        });
    };
    play_->onTuner    = [this] { toggleTuner(); };
    tuner_->onBack    = [this] { if (tunerOpen_) toggleTuner(); };
    addChildComponent (tunerScrim_);
    tunerScrim_.onTap = [this] { if (tunerOpen_) toggleTuner(); };
    addChildComponent (ioScrim_);
    ioScrim_.onTapAt = [this] (juce::Point<int> p) { handleIoPanelTap (p); };
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
            case 5: engine_.setDelayMix (v);   engine_.setDelayEnabled (v > 0.001f); break;
            case 6: engine_.setReverbMix (v);  engine_.setReverbEnabled (v > 0.001f); break;
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
            [this, idx] (bool ok, std::vector<nam::ModelInfo> models, juce::String err) {
                if (! ok) { browse_->setStatus ("Models: " + err); return; }
                if (idx >= (int) browseModels_.size()) return;
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

std::vector<nam::LibraryEntry> AppShell::favDeck() const {
    // Combined favorites deck: models + kept IRs, in the order they were
    // hearted (stable sort keeps store order for ties).
    std::vector<nam::LibraryEntry> deck;
    if (getModels_) for (auto& e : getModels_()) deck.push_back (e);
    if (getIrs_)    for (auto& e : getIrs_())    deck.push_back (e);
    std::stable_sort (deck.begin(), deck.end(),
                      [] (const auto& a, const auto& b) { return a.addedAt < b.addedAt; });
    return deck;
}

void AppShell::showFavCard (int index, bool loadIntoEngine) {
    const auto deck = favDeck();
    if (deck.empty()) {
        collectionIndex_ = -1;
        play_->setNowPlaying ("Nothing kept yet", "NAM PLAYER", {});
        play_->setArtwork ({});
        play_->setKept (false);
        play_->setPosition (-1, 0);
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
    play_->setKept (true);
    play_->setPosition (index, n);
}

void AppShell::showBrowseCard (int index) {
    if (playDeck_.empty()) {
        playDeckIndex_ = -1;
        play_->setNowPlaying ("No results", "TONE3000", {});
        play_->setArtwork ({});
        play_->setKept (false);
        play_->setPosition (-1, 0);
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
    play_->setPosition (index, n);
}

void AppShell::runPlayBrowse (juce::String q) {
    if (! svc_.search) return;
    svc_.search (q, [this] (bool ok, std::vector<nam::ToneInfo> tones, juce::String) {
        if (! ok) return;
        playDeck_ = std::move (tones);
        playDeckIndex_ = playDeck_.empty() ? -1 : 0;
        if (browseView_) showBrowseCard (0);
    });
}

void AppShell::updateCabChoices() {
    juce::StringArray names;
    for (int c = 0; c < nam::demo::kNumCabs; ++c)
        names.add (nam::demo::kCabs[(size_t) c].display);
    cabBuiltinCount_ = names.size();
    if (getIrs_)
        for (const auto& e : getIrs_())
            names.add (juce::String (e.displayName));
    play_->setCabChoices (std::move (names), 0);
}

void AppShell::showEntryAsNowPlaying (const nam::LibraryEntry& e) {
    auditionToneId_.clear();   // a library entry owns the engine now
    browseView_ = false;
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
    if (browseView_) {
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
    // Bottom chrome is just the status orb now (Hi-Fi design): navigation
    // lives in the Play top bar; the orb is the one persistent control.
    navBar_ = b.removeFromBottom (juce::jmax (72, getHeight() / 12));
    for (auto& r : navRects_) r = {};
    const int orbD = juce::jmin (58, navBar_.getHeight() - 10);
    orbRect_ = { navBar_.getCentreX() - orbD / 2,
                 navBar_.getCentreY() - orbD / 2, orbD, orbD };

    // I/O mute panel floats above the nav, centred on the orb.
    const int pw = juce::jmin (380, getWidth() - 48);
    ioPanelRect_ = { getWidth() / 2 - pw / 2, navBar_.getY() - 128 - 10, pw, 128 };
    auto rows = ioPanelRect_.reduced (14, 12);
    ioInRow_  = rows.removeFromTop (52);
    ioOutRow_ = rows.removeFromBottom (52);

    for (juce::Component* c : { (juce::Component*) play_.get(), (juce::Component*) edit_.get(),
                                (juce::Component*) browse_.get(), (juce::Component*) library_.get(),
                                (juce::Component*) live_.get(), (juce::Component*) devices_.get() })
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
                     nam::ui::col::accentAlt, 3.5f);   // design: output = orange
        }

        // Centre: latency, or MUTE when the output is silenced.
        const float bad = juce::jlimit (0.0f, 1.0f, ((float) latencyMs_ - 20.0f) / 30.0f);
        g.setFont (nam::ui::uiFont (outMuted_ ? 9.0f : 10.0f, true));
        g.setColour (outMuted_ ? juce::Colour (0xffff3b30)
                               : nam::ui::col::meterLime.interpolatedWith (
                                     juce::Colour (0xffff3b30), bad));
        g.drawText (outMuted_ ? juce::String ("MUTE")
                              : latencyMs_ > 0.0 ? juce::String ((int) std::round (latencyMs_))
                                                 : juce::String ("--"),
                    orbRect_, juce::Justification::centred, false);
    }

}

void AppShell::paintOverChildren (juce::Graphics& g) {
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
        juce::String inName ("--"), outName ("--");
        if (getDevices_) {
            const auto st = getDevices_();
            inName = shortName (st.currentInput);
            outName = shortName (st.currentOutput);
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
        row (ioOutRow_, "OUTPUT", outName, outMuted_, meterOutPeak_, nam::ui::col::accentAlt);
    }
}

void AppShell::closeIoPanel() {
    if (! ioPanelOpen_) return;
    ioPanelOpen_ = false;
    ioScrim_.setVisible (false);
    repaint();
}

void AppShell::handleIoPanelTap (juce::Point<int> p) {
    // Row taps toggle their side; anything else closes the panel.
    if (ioInRow_.contains (p)) {
        inMuted_ = ! inMuted_;
        if (muteInput_) muteInput_ (inMuted_);
        repaint (ioPanelRect_); repaint (orbRect_.expanded (4));
        return;
    }
    if (ioOutRow_.contains (p)) {
        outMuted_ = ! outMuted_;
        if (muteOutput_) muteOutput_ (outMuted_);
        repaint (ioPanelRect_); repaint (orbRect_.expanded (4));
        return;
    }
    closeIoPanel();
}

void AppShell::mouseDown (const juce::MouseEvent& e) {
    const auto p = e.getPosition();

    if (ioPanelOpen_) {   // taps on the nav while the panel is up just close it
        closeIoPanel();
        return;
    }

    if (orbRect_.expanded (6).contains (p)) {
        ioPanelOpen_ = true;
        ioScrim_.setBounds (contentBounds());
        ioScrim_.setVisible (true);
        ioScrim_.toFront (false);
        repaint();
        return;
    }

    // Tapping the bar anywhere else returns home to Play.
    if (navBar_.contains (p) && current_ != play_.get()) show (Screen::Play);
}
