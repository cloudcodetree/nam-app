#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <vector>
#include "dsp/ToneEngine.h"
#include "model/NamModel.h"

// Minimal Android audio app (Phase 5a, Task 4): device in -> ToneEngine
// (bundled NAM model + gate/EQ/delay/reverb) -> device out, with a status /
// latency / gain UI. No library/network (that is the full Phase 5 port). The
// real payoff is on-device iRig HD X validation (Task 5); on the emulator this
// exercises the launch + audio-device-open + processing path.
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
    std::string copyBundledModelToFile();   // embedded model.nam -> internal file path

    dsp::ToneEngine engine_;
    juce::Slider inGain_, outGain_;
    juce::Label  status_;
    double sampleRate_ = 48000.0;
    int    blockSize_  = 256;
    std::vector<float> mono_;               // preallocated in prepareToPlay
    bool modelLoaded_ = false;
};
