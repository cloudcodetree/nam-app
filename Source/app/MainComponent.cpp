#include "app/MainComponent.h"

#include "model/LibraryImporter.h"

#include <chrono>

MainComponent::MainComponent() {
    library_.load();

    deviceManager_.initialiseWithDefaultDevices(1, 2);
    engine_.prepare(48000, 128);
    deviceManager_.addAudioCallback(&adapter_);
    adapter_.onDeviceChanged = [this](int sr, int mb) {
        // Message-thread reload so the model is re-baked at the device sample rate.
        juce::Component::SafePointer<MainComponent> safe(this);
        juce::MessageManager::callAsync([safe, sr, mb]{
            if (auto* self = safe.getComponent()) {
                self->reloadCurrentModelAt(sr, mb);
                self->reloadCurrentIrAt(sr);
            }
        });
    };

    addAndMakeVisible(selector_);
    addAndMakeVisible(loadButton_);
    addAndMakeVisible(modelLabel_);
    addAndMakeVisible(latencyLabel_);
    addAndMakeVisible(statsLabel_);

#if JUCE_DEBUG
    addAndMakeVisible(inspectButton_);
    inspectButton_.onClick = [this] { inspector_.setVisible(true); };
#endif

    for (auto* s : { &inGain_, &outGain_ }) {
        s->setSliderStyle(juce::Slider::LinearHorizontal);
        s->setRange(-24.0, 24.0, 0.1); s->setValue(0.0);
        s->setTextValueSuffix(" dB");
        addAndMakeVisible(*s);
    }
    inGain_.onValueChange  = [this]{ engine_.setInputDb((float) inGain_.getValue()); };
    outGain_.onValueChange = [this]{ engine_.setOutputDb((float) outGain_.getValue()); };

    loadButton_.onClick = [this]{ loadButtonClicked(); };

    // Noise gate.
    addAndMakeVisible(gateEnable_); gateEnable_.setButtonText("Gate");
    gateEnable_.onClick = [this]{ engine_.setGateEnabled(gateEnable_.getToggleState()); };
    gateThresh_.setSliderStyle(juce::Slider::LinearHorizontal);
    gateThresh_.setRange(-80.0, 0.0, 0.5); gateThresh_.setValue(-60.0);
    gateThresh_.setTextValueSuffix(" dB"); addAndMakeVisible(gateThresh_);
    gateThresh_.onValueChange = [this]{ engine_.setGateThresholdDb((float) gateThresh_.getValue()); };

    // 3-band EQ.
    addAndMakeVisible(eqEnable_); eqEnable_.setButtonText("EQ");
    eqEnable_.onClick = [this]{ engine_.setEqEnabled(eqEnable_.getToggleState()); };
    for (auto* s : { &eqLow_, &eqMid_, &eqHigh_ }) {
        s->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s->setRange(-12.0, 12.0, 0.1); s->setValue(0.0);
        s->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 48, 16);
        addAndMakeVisible(*s);
    }
    eqLow_.onValueChange  = [this]{ engine_.setLowDb((float) eqLow_.getValue()); };
    eqMid_.onValueChange  = [this]{ engine_.setMidDb((float) eqMid_.getValue()); };
    eqHigh_.onValueChange = [this]{ engine_.setHighDb((float) eqHigh_.getValue()); };

    // IR cab.
    addAndMakeVisible(irEnable_); irEnable_.setButtonText("Cab IR");
    irEnable_.onClick = [this]{ engine_.setIrEnabled(irEnable_.getToggleState()); };
    addAndMakeVisible(loadIrButton_); addAndMakeVisible(irLabel_);
    loadIrButton_.onClick = [this]{ loadIrClicked(); };

    // Library panel: favorites/recents list for models + IRs, click-to-load.
    addAndMakeVisible(libraryPanel_);
    libraryPanel_.onLoadEntry = [this](const nam::LibraryEntry& e) { handleLibraryEntryLoad(e); };
    libraryPanel_.refresh();

    // TONE3000 browse/download.
    addAndMakeVisible(browseT3kButton_);
    addAndMakeVisible(t3kStatus_);
    if (!t3kAuth_.isConfigured()) {
        browseT3kButton_.setEnabled(false);
        t3kStatus_.setText("TONE3000 not configured (.env)", juce::dontSendNotification);
    } else {
        browseT3kButton_.onClick = [this] { browseT3kButtonClicked(); };
    }

    // In-app TONE3000 search panel (additive to the Browse TONE3000 button
    // above): query -> results -> pick -> download, reusing t3kAuth_ /
    // t3kSession_ / the Phase 4a download path.
    addAndMakeVisible(searchPanel_);
    if (!t3kAuth_.isConfigured()) {
        searchPanel_.setStatus("TONE3000 not configured (.env)");
    }
    searchPanel_.onSearch = [this](const juce::String& query) { doTone3000Search(query); };
    searchPanel_.onPick = [this](const nam::ToneInfo& tone) { downloadPickedTone(tone); };

    startTimerHz(15);
    setSize(1200, 900);
}

MainComponent::~MainComponent() {
    stopTimer();
    deviceManager_.removeAudioCallback(&adapter_);
}

void MainComponent::loadButtonClicked() {
    chooser_ = std::make_unique<juce::FileChooser>(
        "Select a NAM model", juce::File{}, "*.nam");
    juce::Component::SafePointer<MainComponent> safe(this);
    chooser_->launchAsync(juce::FileBrowserComponent::openMode
                        | juce::FileBrowserComponent::canSelectFiles,
        [safe](const juce::FileChooser& fc) {
            auto* self = safe.getComponent();
            if (self == nullptr) return;
            auto f = fc.getResult();
            if (f == juce::File{}) return;
            self->currentModelPath_ = f.getFullPathName();

            // Copy the picked file into the local library (best-effort; the
            // immediate load below still uses the original path).
            auto* entry = nam::importIntoLibrary(self->library_, self->currentModelPath_.toStdString(),
                                    nam::LibraryType::Model, MainComponent::nowSeconds());
            if (entry != nullptr) {
                self->library_.markUsed(entry->id, MainComponent::nowSeconds());
                self->library_.save();
            }
            self->libraryPanel_.refresh();

            auto* dev = self->deviceManager_.getCurrentAudioDevice();
            self->host_.configure(dev ? (int) dev->getCurrentSampleRate() : 48000,
                            dev ? dev->getCurrentBufferSizeSamples() : 128);
            self->modelLabel_.setText("Loading " + f.getFileName() + "...",
                                juce::dontSendNotification);
            self->host_.requestLoad(self->currentModelPath_.toStdString(),
                [safe, name = f.getFileName()](std::shared_ptr<nam::NamModel> m) {
                    juce::MessageManager::callAsync([safe, m, name]{
                        if (auto* self = safe.getComponent()) {
                            self->engine_.setModel(m);
                            self->modelLabel_.setText(m ? ("Loaded: " + name)
                                                  : ("Failed to load " + name),
                                                juce::dontSendNotification);
                        }
                    });
                });
        });
}

void MainComponent::loadIrClicked() {
    irChooser_ = std::make_unique<juce::FileChooser>(
        "Select a cab IR", juce::File{}, "*.wav");
    juce::Component::SafePointer<MainComponent> safe(this);
    irChooser_->launchAsync(juce::FileBrowserComponent::openMode
                        | juce::FileBrowserComponent::canSelectFiles,
        [safe](const juce::FileChooser& fc) {
            auto* self = safe.getComponent();
            if (self == nullptr) return;
            auto f = fc.getResult();
            if (f == juce::File{}) return;
            self->currentIrPath_ = f.getFullPathName();

            // Copy the picked file into the local library (best-effort; the
            // immediate load below still uses the original path).
            auto* entry = nam::importIntoLibrary(self->library_, self->currentIrPath_.toStdString(),
                                    nam::LibraryType::Ir, MainComponent::nowSeconds());
            if (entry != nullptr) {
                self->library_.markUsed(entry->id, MainComponent::nowSeconds());
                self->library_.save();
            }
            self->libraryPanel_.refresh();

            auto* dev = self->deviceManager_.getCurrentAudioDevice();
            const int sr = dev ? (int) dev->getCurrentSampleRate() : 48000;

            // IR files are tiny, so loading synchronously here (already off
            // the UI paint path, inside the async chooser callback) is fine.
            auto ir = nam::loadImpulseResponse(self->currentIrPath_.toStdString(), sr, dsp::kMaxIrTaps);

            juce::MessageManager::callAsync([safe, ir, name = f.getFileName()]{
                if (auto* self2 = safe.getComponent()) {
                    self2->engine_.setImpulse(ir);
                    self2->irLabel_.setText(ir ? ("Loaded: " + name)
                                          : ("Failed to load " + name),
                                        juce::dontSendNotification);
                }
            });
        });
}

void MainComponent::browseT3kButtonClicked() {
    // Disable the button for the whole flow so a second click can't stack a
    // second auth/download flow (or Tone3000Session) on top of this one --
    // that's what makes downloadToneModel()'s stopThread(20000) stall the
    // message thread. Re-enabled on every completion path below.
    browseT3kButton_.setEnabled(false);
    t3kStatus_.setText("Working... (check your browser)", juce::dontSendNotification);

    juce::Component::SafePointer<MainComponent> safe(this);
    t3kAuth_.beginSelectToneFlow([safe](nam::Tone3000Auth::Result result) {
        auto* self = safe.getComponent();
        if (self == nullptr) return;

        if (!result.ok) {
            // result.error never contains the token/code (see Tone3000Auth).
            self->t3kStatus_.setText("TONE3000: " + juce::String(result.error),
                                      juce::dontSendNotification);
            self->browseT3kButton_.setEnabled(true);
            return;
        }
        if (result.toneId.empty()) {
            self->t3kStatus_.setText("TONE3000: no tone was selected", juce::dontSendNotification);
            self->browseT3kButton_.setEnabled(true);
            return;
        }

        self->t3kStatus_.setText("Downloading model from TONE3000...", juce::dontSendNotification);
        self->t3kSession_ = std::make_unique<nam::Tone3000Session>(self->t3kAuth_.accessToken());

        const auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
        self->t3kSession_->downloadToneModel(result.toneId, tempDir,
            [safe](bool ok, juce::File file, juce::String nameOrError) {
                auto* self2 = safe.getComponent();
                if (self2 == nullptr) return;

                if (!ok) {
                    // nameOrError is Tone3000Session's error message, which
                    // never contains the access token.
                    self2->t3kStatus_.setText("TONE3000 download failed: " + nameOrError,
                                               juce::dontSendNotification);
                    self2->browseT3kButton_.setEnabled(true);
                    return;
                }

                auto* entry = nam::importIntoLibrary(self2->library_, file.getFullPathName().toStdString(),
                                    nam::LibraryType::Model, MainComponent::nowSeconds());
                file.deleteFile(); // best-effort cleanup of the temp download

                if (entry == nullptr) {
                    self2->t3kStatus_.setText("Failed to import the downloaded model into the library",
                                               juce::dontSendNotification);
                    self2->browseT3kButton_.setEnabled(true);
                    return;
                }

                // handleLibraryEntryLoad() marks it used, saves, refreshes
                // the library panel, and loads it -- the same path the
                // library panel's click-to-load uses.
                self2->handleLibraryEntryLoad(*entry);
                self2->t3kStatus_.setText("Loaded from TONE3000: " + juce::String(entry->displayName),
                                           juce::dontSendNotification);
                self2->browseT3kButton_.setEnabled(true);
            });
    });
}

void MainComponent::doTone3000Search(const juce::String& query) {
    if (!t3kAuth_.isConfigured()) {
        searchPanel_.setStatus("TONE3000 not configured (.env)");
        return;
    }

    searchPanel_.setStatus("Searching...");

    juce::Component::SafePointer<MainComponent> safe(this);
    auto searchDone = [safe](bool ok, std::vector<nam::ToneInfo> tones, juce::String error) {
        auto* self = safe.getComponent();
        if (self == nullptr) return;

        if (!ok) {
            // `error` is Tone3000Session's error message, which never
            // contains the access token.
            self->searchPanel_.setStatus("TONE3000 search failed: " + error);
            return;
        }
        const int n = (int) tones.size();
        self->searchPanel_.setResults(std::move(tones));
        self->searchPanel_.setStatus(juce::String(n) + (n == 1 ? " result" : " results"));
    };

    if (t3kAuth_.hasValidToken()) {
        if (t3kSession_ == nullptr)
            t3kSession_ = std::make_unique<nam::Tone3000Session>(t3kAuth_.accessToken());
        t3kSession_->search(query.toStdString(), 1, searchDone);
        return;
    }

    // No valid token yet: try a silent refresh first (no browser) using a
    // stored refresh token. Only if that's impossible/fails do we fall back
    // to the browser Connect flow.
    t3kAuth_.tryRefresh([safe, query, searchDone](bool refreshed) {
        auto* self = safe.getComponent();
        if (self == nullptr) return;

        if (refreshed) {
            self->t3kSession_ = std::make_unique<nam::Tone3000Session>(self->t3kAuth_.accessToken());
            self->t3kSession_->search(query.toStdString(), 1, searchDone);
            return;
        }

        // Refresh wasn't possible (no refresh token) or failed: authenticate
        // via the browser (token-only, no select_tone prompt), then run the
        // search once connected.
        self->t3kAuth_.beginConnectFlow([safe, query, searchDone](nam::Tone3000Auth::Result result) {
            auto* self2 = safe.getComponent();
            if (self2 == nullptr) return;

            if (!result.ok) {
                // result.error never contains the token/code (see Tone3000Auth).
                self2->searchPanel_.setStatus("TONE3000: " + juce::String(result.error));
                return;
            }

            self2->t3kSession_ = std::make_unique<nam::Tone3000Session>(self2->t3kAuth_.accessToken());
            self2->t3kSession_->search(query.toStdString(), 1, searchDone);
        });
    });
}

void MainComponent::downloadPickedTone(const nam::ToneInfo& tone) {
    if (!t3kAuth_.hasValidToken()) {
        searchPanel_.setStatus("Connect first (run a search to connect)");
        return;
    }

    searchPanel_.setStatus("Downloading model from TONE3000...");
    if (t3kSession_ == nullptr)
        t3kSession_ = std::make_unique<nam::Tone3000Session>(t3kAuth_.accessToken());

    const auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    juce::Component::SafePointer<MainComponent> safe(this);
    t3kSession_->downloadToneModel(tone.id, tempDir,
        [safe](bool ok, juce::File file, juce::String nameOrError) {
            auto* self = safe.getComponent();
            if (self == nullptr) return;

            if (!ok) {
                // nameOrError is Tone3000Session's error message, which
                // never contains the access token.
                self->searchPanel_.setStatus("TONE3000 download failed: " + nameOrError);
                return;
            }

            auto* entry = nam::importIntoLibrary(self->library_, file.getFullPathName().toStdString(),
                                nam::LibraryType::Model, MainComponent::nowSeconds());
            file.deleteFile(); // best-effort cleanup of the temp download

            if (entry == nullptr) {
                self->searchPanel_.setStatus("Failed to import the downloaded model into the library");
                return;
            }

            // handleLibraryEntryLoad() marks it used, saves, refreshes the
            // library panel, and loads it -- the same path the library
            // panel's and Browse TONE3000's click-to-load use.
            self->handleLibraryEntryLoad(*entry);
            self->searchPanel_.setStatus("Downloaded: " + juce::String(entry->displayName));
        });
}

void MainComponent::reloadCurrentModelAt(int sampleRate, int maxBlock) {
    if (currentModelPath_.isEmpty()) return;
    host_.configure(sampleRate, maxBlock);
    host_.requestLoad(currentModelPath_.toStdString(),
        [this](std::shared_ptr<nam::NamModel> m) {
            juce::Component::SafePointer<MainComponent> safe(this);
            juce::MessageManager::callAsync([safe, m]{
                if (auto* self = safe.getComponent())
                    self->engine_.setModel(m);
            });
        });
}

void MainComponent::reloadCurrentIrAt(int sampleRate) {
    if (currentIrPath_.isEmpty()) return;
    auto ir = nam::loadImpulseResponse(currentIrPath_.toStdString(), sampleRate, dsp::kMaxIrTaps);
    engine_.setImpulse(ir);
}

std::string MainComponent::defaultLibraryDir() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("NAM Player/library").getFullPathName().toStdString();
}

long long MainComponent::nowSeconds() {
    using namespace std::chrono;
    return (long long) duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

void MainComponent::handleLibraryEntryLoad(const nam::LibraryEntry& e) {
    const std::string absolutePath = library_.subdir(e.type) + "/" + e.fileName;

    if (e.type == nam::LibraryType::Model) {
        currentModelPath_ = juce::String(absolutePath);
        auto* dev = deviceManager_.getCurrentAudioDevice();
        host_.configure(dev ? (int) dev->getCurrentSampleRate() : 48000,
                        dev ? dev->getCurrentBufferSizeSamples() : 128);
        modelLabel_.setText("Loading " + juce::String(e.displayName) + "...",
                            juce::dontSendNotification);
        juce::Component::SafePointer<MainComponent> safe(this);
        host_.requestLoad(absolutePath,
            [safe, name = juce::String(e.displayName)](std::shared_ptr<nam::NamModel> m) {
                juce::MessageManager::callAsync([safe, m, name]{
                    if (auto* self = safe.getComponent()) {
                        self->engine_.setModel(m);
                        self->modelLabel_.setText(m ? ("Loaded: " + name)
                                              : ("Failed to load " + name),
                                            juce::dontSendNotification);
                    }
                });
            });
    } else {
        currentIrPath_ = juce::String(absolutePath);
        auto* dev = deviceManager_.getCurrentAudioDevice();
        const int sr = dev ? (int) dev->getCurrentSampleRate() : 48000;
        auto ir = nam::loadImpulseResponse(absolutePath, sr, dsp::kMaxIrTaps);
        engine_.setImpulse(ir);
        irLabel_.setText(ir ? ("Loaded: " + juce::String(e.displayName))
                             : ("Failed to load " + juce::String(e.displayName)),
                          juce::dontSendNotification);
        irEnable_.setToggleState(true, juce::sendNotification);
        engine_.setIrEnabled(true);
    }

    library_.markUsed(e.id, nowSeconds());
    library_.save();
    libraryPanel_.refresh();
}

void MainComponent::timerCallback() {
    if (auto* dev = deviceManager_.getCurrentAudioDevice()) {
        const double sr = dev->getCurrentSampleRate();
        const int    bs = dev->getCurrentBufferSizeSamples();
        const double ms = sr > 0 ? (bs / sr) * 1000.0 : 0.0;
        juce::String warn = (sr > 0 && bs / sr > 0.02) ? "  (high latency)" : "";
        latencyLabel_.setText(juce::String(bs) + " smp @ " + juce::String((int) sr)
            + " Hz  ~" + juce::String(ms, 1) + " ms/dir" + warn,
            juce::dontSendNotification);
    }

    // Read lock-free telemetry the audio thread published (no lock/no stall).
    const float    peak   = engine_.outputPeak();
    const float    load   = engine_.cpuLoad();
    const uint64_t blocks = engine_.blockCount();
    const uint32_t xruns  = engine_.overCapacityCount();

    // Peak meter with a decay so it reads like an analog meter.
    const float peakDb = peak > 1.0e-6f ? juce::Decibels::gainToDecibels(peak) : -100.0f;
    meterDb_ = juce::jmax(peakDb, meterDb_ - 3.0f);   // ~fast attack, decay 3 dB/tick

    statsLabel_.setText("load " + juce::String(load * 100.0f, 0) + "%   "
        + "blocks " + juce::String((juce::int64) blocks) + "   "
        + "xruns " + juce::String((int) xruns),
        juce::dontSendNotification);

    repaint(meterBounds_);
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black);

    // Output level meter: -60..0 dB mapped across meterBounds_.
    if (! meterBounds_.isEmpty()) {
        auto r = meterBounds_.toFloat();
        g.setColour(juce::Colours::darkgrey.darker());
        g.fillRoundedRectangle(r, 3.0f);
        const float norm = juce::jlimit(0.0f, 1.0f, (meterDb_ + 60.0f) / 60.0f);
        auto fill = r.withWidth(r.getWidth() * norm);
        const juce::Colour c = meterDb_ > -3.0f ? juce::Colours::red
                             : meterDb_ > -12.0f ? juce::Colours::yellow
                                                 : juce::Colours::limegreen;
        g.setColour(c);
        g.fillRoundedRectangle(fill, 3.0f);
        g.setColour(juce::Colours::white.withAlpha(0.7f));
        g.setFont(12.0f);
        g.drawText(juce::String(meterDb_, 1) + " dB", meterBounds_,
                   juce::Justification::centred);
    }
}

void MainComponent::resized() {
    auto full = getLocalBounds().reduced(12);

    auto searchArea = full.removeFromRight(300);
    full.removeFromRight(12);
    searchPanel_.setBounds(searchArea);

    auto libraryArea = full.removeFromRight(300);
    full.removeFromRight(12);
    libraryPanel_.setBounds(libraryArea);

    auto r = full;
    selector_.setBounds(r.removeFromTop(300));
    r.removeFromTop(8);
    loadButton_.setBounds(r.removeFromTop(32));
    modelLabel_.setBounds(r.removeFromTop(24));
    latencyLabel_.setBounds(r.removeFromTop(24));
    r.removeFromTop(8);
    inGain_.setBounds(r.removeFromTop(32));
    outGain_.setBounds(r.removeFromTop(32));
    r.removeFromTop(8);

    // Noise gate row.
    {
        auto row = r.removeFromTop(32);
        gateEnable_.setBounds(row.removeFromLeft(80));
        gateThresh_.setBounds(row);
    }
    r.removeFromTop(8);

    // EQ row: enable + 3 rotary knobs.
    {
        auto row = r.removeFromTop(90);
        eqEnable_.setBounds(row.removeFromLeft(80));
        const int knobW = row.getWidth() / 3;
        eqLow_.setBounds(row.removeFromLeft(knobW));
        eqMid_.setBounds(row.removeFromLeft(knobW));
        eqHigh_.setBounds(row);
    }
    r.removeFromTop(8);

    // IR cab row.
    {
        auto row = r.removeFromTop(32);
        irEnable_.setBounds(row.removeFromLeft(80));
        loadIrButton_.setBounds(row.removeFromLeft(140));
        row.removeFromLeft(8);
        irLabel_.setBounds(row);
    }
    r.removeFromTop(8);

    // TONE3000 browse/download row.
    {
        auto row = r.removeFromTop(32);
        browseT3kButton_.setBounds(row.removeFromLeft(160));
        row.removeFromLeft(8);
        t3kStatus_.setBounds(row);
    }
    r.removeFromTop(8);

    meterBounds_ = r.removeFromTop(22);
    statsLabel_.setBounds(r.removeFromTop(22));
#if JUCE_DEBUG
    r.removeFromTop(8);
    inspectButton_.setBounds(r.removeFromTop(28).removeFromLeft(120));
#endif
}
