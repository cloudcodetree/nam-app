#include "app/ui/AppShell.h"

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

    for (juce::Component* c : { (juce::Component*) play_.get(), (juce::Component*) edit_.get(),
                                (juce::Component*) browse_.get(), (juce::Component*) library_.get(),
                                (juce::Component*) live_.get(), (juce::Component*) devices_.get() })
        addChildComponent (*c);

    play_->onNav = [this] (int tab) {
        switch (tab) { case 1: show (Screen::Edit);   break;
                       case 2: show (Screen::Browse); break;
                       case 3: show (Screen::Live);   break;
                       default: show (Screen::Play);  break; }
    };
    play_->onLibrary  = [this] { show (Screen::Library); };
    play_->onSettings = [this] { show (Screen::Devices); };
    play_->onPrev     = [this] { stepCollection (-1); };
    play_->onNext     = [this] { stepCollection (+1); };
    devices_->onBack  = [this] { show (Screen::Play); };
    edit_->onDone    = [this] { show (Screen::Play); };
    browse_->onBack  = [this] { show (Screen::Play); };
    library_->onBack = [this] { show (Screen::Play); };
    live_->onExit    = [this] { show (Screen::Play); };

    devices_->onSetInputDb = [this] (float db) { engine_.setInputDb (db); };

    show (Screen::Play);
}

void AppShell::setBrowseServices (BrowseServices services) {
    svc_ = std::move (services);

    browse_->onQuery = [this] (juce::String q) { runBrowseSearch (std::move (q)); };

    browse_->onKeep = [this] (int idx) {
        if (! svc_.keep || idx < 0 || idx >= (int) browseResults_.size()) return;
        const auto tone = browseResults_[(size_t) idx];
        browse_->setStatus ("Downloading \"" + juce::String (tone.title) + "\"" + kEllipsis);
        svc_.keep (tone, [this, idx] (bool ok, juce::String msg) {
            if (ok) browse_->setKept (idx);
            browse_->setStatus (ok ? (kHeart + " Kept: " + msg) : ("Download failed: " + msg));
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
                            [this, idx, modelIdx] (bool ok, juce::String msg) {
            browse_->setLoading (-1, 0.0f);
            if (! ok) { browse_->setStatus ("Audition failed: " + msg); return; }
            auditioningPack_ = idx;
            auditioningModel_ = modelIdx;
            browse_->setPlaying (idx, modelIdx);
            browse_->setStatus ("Auditioning \"" + msg + "\"");
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

void AppShell::showEntryAsNowPlaying (const nam::LibraryEntry& e) {
    int idx = -1, count = 0;
    if (getModels_) {
        const auto entries = getModels_();
        count = (int) entries.size();
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].id == e.id) { idx = (int) i; break; }
    }
    collectionIndex_ = idx;
    // Raw arch strings are internal ("SlimmableContainer" etc.) — show the
    // TONE3000 generation label instead.
    juce::String family ("MY TONES");
    if (! e.arch.empty()) {
        const auto a = juce::String (e.arch).toLowerCase();
        const bool isA2 = a.contains ("slim") || a.startsWith ("2") || a.startsWith ("a2");
        family = juce::String (isA2 ? "A2" : "A1") + " " + kDotSep + " MY TONES";
    }
    play_->setNowPlaying (juce::String (e.displayName), family, {});
    play_->setPosition (idx, count);
}

void AppShell::stepCollection (int delta) {
    if (! getModels_ || ! loadModel_) return;
    const auto entries = getModels_();
    if (entries.empty()) {
        play_->setNowPlaying ("Bundled Tone", "NAM PLAYER", {});
        play_->setPosition (-1, 0);
        return;
    }
    const int n = (int) entries.size();
    const int idx = collectionIndex_ < 0
        ? (delta > 0 ? 0 : n - 1)
        : ((collectionIndex_ + delta) % n + n) % n;
    loadModel_ (entries[(size_t) idx]);
    showEntryAsNowPlaying (entries[(size_t) idx]);
}

void AppShell::runBrowseSearch (juce::String q) {
    if (! svc_.search) return;
    stopAudition();
    browse_->setStatus (q.isEmpty() ? ("Tuning in to TONE3000" + kEllipsis)
                                    : ("Searching \"" + q + "\"" + kEllipsis));
    svc_.search (q, [this] (bool ok, std::vector<nam::ToneInfo> tones, juce::String err) {
        if (! ok) { browse_->setStatus ("TONE3000: " + err); return; }
        browseResults_ = tones;
        browseModels_.assign (tones.size(), {});
        browse_->setResults (tones);
        refreshCachedFlags();
        browse_->setStatus (juce::String ((int) tones.size())
                            + (tones.size() == 1 ? " pack" : " packs")
                            + " " + kDotSep + " tap a pack to expand " + kDotSep
                            + " " + kHeart + " keeps");
    });
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
    if (current_ != nullptr && current_ != play_.get()) { show (Screen::Play); return true; }
    return false;
}

void AppShell::show (Screen s) {
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

    juce::Component* target = play_.get();
    switch (s) {
        case Screen::Edit:    target = edit_.get();    break;
        case Screen::Browse:  target = browse_.get();  break;
        case Screen::Library: target = library_.get(); break;
        case Screen::Live:    target = live_.get();    break;
        case Screen::Devices: target = devices_.get(); break;
        case Screen::Play:    default: target = play_.get(); break;
    }
    if (current_ == target) return;
    if (current_ != nullptr) current_->setVisible (false);
    current_ = target;
    current_->setBounds (getLocalBounds());
    current_->setVisible (true);
    current_->toFront (false);
}

void AppShell::setLevels (float in, float out) {
    if (play_ != nullptr)    play_->setLevels (in, out);
    if (devices_ != nullptr) devices_->setLevels (in, out);
}

void AppShell::resized() {
    auto b = getLocalBounds();
    for (juce::Component* c : { (juce::Component*) play_.get(), (juce::Component*) edit_.get(),
                                (juce::Component*) browse_.get(), (juce::Component*) library_.get(),
                                (juce::Component*) live_.get(), (juce::Component*) devices_.get() })
        if (c != nullptr) c->setBounds (b);
}
