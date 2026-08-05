#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <memory>
#include <vector>
#include "dsp/ToneEngine.h"
#include "model/NamModel.h"
#include "app/ui/NamLookAndFeel.h"
#include "app/ui/PlayScreen.h"

// Android app shell (Phase 5a): owns the audio device + ToneEngine and hosts
// the SHARED, cross-platform Hi-Fi UI (Source/app/ui). The screens themselves
// are platform-agnostic JUCE and are meant to be reused by the desktop/iOS
// shells too — only this AudioAppComponent glue is Android-oriented.
class AndroidAudioApp : public juce::AudioAppComponent,
                        private juce::Timer {
public:
    AndroidAudioApp();
    ~AndroidAudioApp() override;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override;
    void releaseResources() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    std::string copyBundledModelToFile();

    nam::ui::NamLookAndFeel laf_;
    dsp::ToneEngine engine_;
    std::unique_ptr<PlayScreen> play_;

    double sampleRate_ = 48000.0;
    int    blockSize_  = 256;
    std::vector<float> mono_;                 // preallocated in prepareToPlay
    std::atomic<float> inPeak_ { 0.0f };      // audio thread -> UI timer
    bool modelLoaded_ = false;
};
