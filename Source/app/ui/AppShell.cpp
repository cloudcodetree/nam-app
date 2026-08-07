#include "app/ui/AppShell.h"
#include "app/ui/NamLookAndFeel.h"

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

    for (juce::Component* c : { (juce::Component*) play_.get(), (juce::Component*) edit_.get(),
                                (juce::Component*) browse_.get(), (juce::Component*) library_.get(),
                                (juce::Component*) live_.get(), (juce::Component*) devices_.get(),
                                (juce::Component*) tuner_.get() })
        addChildComponent (*c);

    play_->onLibrary  = [this] { show (Screen::Library); };
    play_->onSettings = [this] { show (Screen::Devices); };
    play_->onPrev     = [this] { stepCollection (-1); };
    play_->onNext     = [this] { stepCollection (+1); };
    play_->onTuner    = [this] { show (Screen::Tuner); };
    tuner_->onBack    = [this] { show (Screen::Play); };
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
        case Screen::Tuner:   target = tuner_.get();   break;
        case Screen::Play:    default: target = play_.get(); break;
    }
    // Persistent chrome: which nav tab is lit for this screen.
    switch (s) {
        case Screen::Play:    activeTab_ = 0; break;
        case Screen::Edit:    activeTab_ = 1; break;
        case Screen::Browse:  activeTab_ = 2; break;
        case Screen::Live:    activeTab_ = 3; break;
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
    if (std::abs (in - meterInPeak_) > 0.005f) {
        meterInPeak_ = in;
        repaint (meterBar_);
    }
}

void AppShell::setTunerPitch (float hz) {
    if (play_ == nullptr) return;
    if (tuner_ != nullptr) tuner_->setPitch (hz);
    if (hz <= 0.0f) { play_->setTuner ({}, 0.0f, false); return; }
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
    b.removeFromTop (meterBar_.getHeight());
    b.removeFromBottom (navBar_.getHeight());
    return b;
}

void AppShell::resized() {
    auto b = getLocalBounds();
    meterBar_ = b.removeFromTop (12);
    navBar_   = b.removeFromBottom (juce::jmax (64, getHeight() / 13));
    const int nw = navBar_.getWidth() / 4;
    for (int i = 0; i < 4; ++i)
        navRects_[(size_t) i] = { navBar_.getX() + i * nw, navBar_.getY(), nw, navBar_.getHeight() };

    for (juce::Component* c : { (juce::Component*) play_.get(), (juce::Component*) edit_.get(),
                                (juce::Component*) browse_.get(), (juce::Component*) library_.get(),
                                (juce::Component*) live_.get(), (juce::Component*) devices_.get(),
                                (juce::Component*) tuner_.get() })
        if (c != nullptr) c->setBounds (b);
}

void AppShell::paint (juce::Graphics& g) {
    // Global bottom chrome only — screens paint everything above it.
    g.setColour (nam::ui::col::bg);
    g.fillRect (meterBar_.getUnion (navBar_));

    // Slim input meter (dB-scaled -60..0), full-width.
    {
        auto bar = meterBar_.reduced (20, 4).toFloat();
        g.setColour (nam::ui::col::inkA (0.08f));
        g.fillRoundedRectangle (bar, 2.0f);
        const float db = meterInPeak_ > 0.001f ? 20.0f * std::log10 (meterInPeak_) : -60.0f;
        const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        if (frac > 0.0f) {
            const float w = juce::jmax (3.0f, bar.getWidth() * frac);
            juce::ColourGradient mg (nam::ui::col::meterGreen, bar.getX(), 0,
                                     nam::ui::col::meterLime, bar.getX() + w, 0, false);
            g.setGradientFill (mg);
            juce::Path p; p.addRoundedRectangle (bar.withWidth (w), 2.0f);
            g.fillPath (p);
        }
    }

    // Persistent nav
    static const char* labels[] = { "PLAY", "EDIT", "TONES", "LIVE" };
    static const char* glyphsUtf8[] = { "\xE2\x96\xB6", "\xE2\x9C\x8E", "\xE2\x97\x89", "\xE2\x89\xA1" };
    for (int i = 0; i < 4; ++i) {
        const bool active = (i == activeTab_);
        const auto c = active ? nam::ui::col::accent : nam::ui::col::inkA (0.45f);
        auto cell = navRects_[(size_t) i];
        auto icon = cell.removeFromTop (cell.getHeight() * 6 / 10);
        g.setFont (nam::ui::uiFont (16.0f, false));
        g.setColour (c);
        g.drawText (juce::String::fromUTF8 (glyphsUtf8[i]), icon.withTrimmedTop (8),
                    juce::Justification::centredBottom, false);
        g.setFont (nam::ui::uiFontTracked (10.0f, true));
        g.drawText (labels[i], cell, juce::Justification::centredTop, false);
    }
}

void AppShell::mouseDown (const juce::MouseEvent& e) {
    const auto p = e.getPosition();
    for (int i = 0; i < 4; ++i)
        if (navRects_[(size_t) i].contains (p)) {
            switch (i) {
                case 1: show (Screen::Edit);   break;
                case 2: show (Screen::Browse); break;
                case 3: show (Screen::Live);   break;
                default: show (Screen::Play);  break;
            }
            return;
        }
    if (meterBar_.contains (p)) show (Screen::Devices);
}
