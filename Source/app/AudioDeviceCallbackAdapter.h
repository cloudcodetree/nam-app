#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <functional>
#include "dsp/ToneEngine.h"

class AudioDeviceCallbackAdapter : public juce::AudioIODeviceCallback {
public:
    explicit AudioDeviceCallbackAdapter(dsp::ToneEngine& engine) : engine_(engine) {}
    std::function<void(int, int)> onDeviceChanged;

    void audioDeviceIOCallbackWithContext(const float* const* in, int numIn,
        float* const* out, int numOut, int numSamples,
        const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    dsp::ToneEngine& engine_;
    int sampleRate_ = 48000;
    int maxBlock_   = 128;
};
