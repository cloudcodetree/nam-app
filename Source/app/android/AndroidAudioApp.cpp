#include "app/android/AndroidAudioApp.h"

#include "BinaryData.h"

#include <algorithm>

AndroidAudioApp::AndroidAudioApp() {
    addAndMakeVisible(status_);
    status_.setColour(juce::Label::textColourId, juce::Colours::limegreen);
    status_.setJustificationType(juce::Justification::topLeft);
    status_.setFont(juce::Font(juce::FontOptions(18.0f)));

    for (auto* s : { &inGain_, &outGain_ }) {
        s->setSliderStyle(juce::Slider::LinearHorizontal);
        s->setRange(-24.0, 24.0, 0.1);
        s->setValue(0.0);
        s->setTextValueSuffix(" dB");
        addAndMakeVisible(*s);
    }
    inGain_.onValueChange  = [this]{ engine_.setInputDb((float) inGain_.getValue()); };
    outGain_.onValueChange = [this]{ engine_.setOutputDb((float) outGain_.getValue()); };

    // 1 input (guitar) / 2 output (stereo). JUCE requests RECORD_AUDIO when it
    // opens the input device on Android.
    setAudioChannels(1, 2);
    setSize(900, 500);
    startTimerHz(10);
}

AndroidAudioApp::~AndroidAudioApp() {
    stopTimer();
    shutdownAudio();
}

std::string AndroidAudioApp::copyBundledModelToFile() {
    // The model is embedded via juce_add_binary_data (name "model.nam" ->
    // resource "model_nam"). NamModel::load() reads a filesystem path, so copy
    // the embedded bytes to internal storage once.
    auto dest = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("model.nam");
    if (! dest.existsAsFile()) {
        int size = 0;
        if (const char* data = BinaryData::getNamedResource("model_nam", size))
            dest.replaceWithData(data, (size_t) size);
    }
    return dest.getFullPathName().toStdString();
}

void AndroidAudioApp::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    sampleRate_ = sampleRate;
    blockSize_  = samplesPerBlockExpected;
    mono_.assign((size_t) juce::jmax(1, samplesPerBlockExpected), 0.0f);
    engine_.prepare((int) sampleRate, samplesPerBlockExpected);

    if (auto m = nam::NamModel::load(copyBundledModelToFile(),
                                     (int) sampleRate, samplesPerBlockExpected)) {
        engine_.setModel(std::move(m));
        modelLoaded_ = true;
    }
}

void AndroidAudioApp::getNextAudioBlock(const juce::AudioSourceChannelInfo& info) {
    auto* buf = info.buffer;
    const int n = info.numSamples;
    const int cap = (int) mono_.size();

    // Fold input channel 0 into the mono scratch, run the tone chain in place,
    // then fan the result out to every output channel.
    const float* in = buf->getReadPointer(0, info.startSample);
    for (int i = 0; i < n && i < cap; ++i) mono_[(size_t) i] = in[i];

    engine_.render(mono_.data(), mono_.data(), std::min(n, cap));

    for (int ch = 0; ch < buf->getNumChannels(); ++ch) {
        float* out = buf->getWritePointer(ch, info.startSample);
        for (int i = 0; i < n && i < cap; ++i) out[i] = mono_[(size_t) i];
    }
}

void AndroidAudioApp::releaseResources() {}

void AndroidAudioApp::timerCallback() {
    const double latMs = sampleRate_ > 0 ? (blockSize_ / sampleRate_) * 1000.0 : 0.0;
    juce::String dev = "no device";
    if (auto* d = deviceManager.getCurrentAudioDevice())
        dev = d->getName() + " (" + d->getTypeName() + ")";

    status_.setText(
        juce::String("NAM Player - Android audio\n\n")
        + "Device: " + dev + "\n"
        + "Model: " + (modelLoaded_ ? juce::String("loaded") : juce::String("NOT loaded")) + "\n"
        + "SR: " + juce::String(sampleRate_, 0) + " Hz   block: " + juce::String(blockSize_) + "\n"
        + "~buffer latency: " + juce::String(latMs, 1) + " ms/dir\n"
        + "CPU load: " + juce::String(engine_.cpuLoad() * 100.0f, 0) + "%\n"
        + "out peak: " + juce::String(engine_.outputPeak(), 3),
        juce::dontSendNotification);
}

void AndroidAudioApp::paint(juce::Graphics& g) { g.fillAll(juce::Colours::black); }

void AndroidAudioApp::resized() {
    auto r = getLocalBounds().reduced(20);
    status_.setBounds(r.removeFromTop(240));
    r.removeFromTop(20);
    inGain_.setBounds(r.removeFromTop(56));
    r.removeFromTop(12);
    outGain_.setBounds(r.removeFromTop(56));
}
