#include "app/android/AndroidAudioApp.h"

#include "BinaryData.h"

#include <algorithm>
#include <cmath>

AndroidAudioApp::AndroidAudioApp() {
    setLookAndFeel(&laf_);

    play_ = std::make_unique<PlayScreen>();
    addAndMakeVisible(*play_);
    play_->onNav     = [](int /*tab*/) { /* screen nav wired in a later screen */ };
    play_->onLibrary = []             { /* library screen wired later */ };

    // 1 input (guitar) / 2 output. JUCE requests RECORD_AUDIO on input open.
    setAudioChannels(1, 2);
    setSize(900, 500);
    startTimerHz(30);
}

AndroidAudioApp::~AndroidAudioApp() {
    stopTimer();
    shutdownAudio();
    setLookAndFeel(nullptr);
}

std::string AndroidAudioApp::copyBundledModelToFile() {
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

    const float* in = buf->getReadPointer(0, info.startSample);
    float inPk = 0.0f;
    for (int i = 0; i < n && i < cap; ++i) {
        mono_[(size_t) i] = in[i];
        inPk = juce::jmax(inPk, std::fabs(in[i]));
    }
    inPeak_.store(inPk, std::memory_order_relaxed);

    engine_.render(mono_.data(), mono_.data(), std::min(n, cap));

    for (int ch = 0; ch < buf->getNumChannels(); ++ch) {
        float* out = buf->getWritePointer(ch, info.startSample);
        for (int i = 0; i < n && i < cap; ++i) out[i] = mono_[(size_t) i];
    }
}

void AndroidAudioApp::releaseResources() {}

void AndroidAudioApp::timerCallback() {
    if (play_ != nullptr)
        play_->setLevels(inPeak_.load(std::memory_order_relaxed),
                         engine_.outputPeak());
}

void AndroidAudioApp::paint(juce::Graphics& g) { g.fillAll(nam::ui::col::bg); }

void AndroidAudioApp::resized() {
    if (play_ != nullptr) play_->setBounds(getLocalBounds());
}
