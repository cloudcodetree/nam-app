#include "app/MainComponent.h"

MainComponent::MainComponent() {
    deviceManager_.initialiseWithDefaultDevices(1, 2);
    engine_.prepare(48000, 128);
    deviceManager_.addAudioCallback(&adapter_);
    adapter_.onDeviceChanged = [this](int sr, int mb) {
        // Message-thread reload so the model is re-baked at the device sample rate.
        juce::Component::SafePointer<MainComponent> safe(this);
        juce::MessageManager::callAsync([safe, sr, mb]{
            if (auto* self = safe.getComponent())
                self->reloadCurrentModelAt(sr, mb);
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
    startTimerHz(15);
    setSize(560, 640);
}

MainComponent::~MainComponent() {
    stopTimer();
    deviceManager_.removeAudioCallback(&adapter_);
}

void MainComponent::loadButtonClicked() {
    chooser_ = std::make_unique<juce::FileChooser>(
        "Select a NAM model", juce::File{}, "*.nam");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode
                        | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc) {
            auto f = fc.getResult();
            if (f == juce::File{}) return;
            currentModelPath_ = f.getFullPathName();
            auto* dev = deviceManager_.getCurrentAudioDevice();
            host_.configure(dev ? (int) dev->getCurrentSampleRate() : 48000,
                            dev ? dev->getCurrentBufferSizeSamples() : 128);
            modelLabel_.setText("Loading " + f.getFileName() + "...",
                                juce::dontSendNotification);
            host_.requestLoad(currentModelPath_.toStdString(),
                [this, name = f.getFileName()](std::shared_ptr<nam::NamModel> m) {
                    juce::Component::SafePointer<MainComponent> safe(this);
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
    auto r = getLocalBounds().reduced(12);
    selector_.setBounds(r.removeFromTop(300));
    r.removeFromTop(8);
    loadButton_.setBounds(r.removeFromTop(32));
    modelLabel_.setBounds(r.removeFromTop(24));
    latencyLabel_.setBounds(r.removeFromTop(24));
    r.removeFromTop(8);
    inGain_.setBounds(r.removeFromTop(32));
    outGain_.setBounds(r.removeFromTop(32));
    r.removeFromTop(8);
    meterBounds_ = r.removeFromTop(22);
    statsLabel_.setBounds(r.removeFromTop(22));
#if JUCE_DEBUG
    r.removeFromTop(8);
    inspectButton_.setBounds(r.removeFromTop(28).removeFromLeft(120));
#endif
}
