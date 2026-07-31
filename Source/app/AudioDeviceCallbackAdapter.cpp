#include "app/AudioDeviceCallbackAdapter.h"

void AudioDeviceCallbackAdapter::audioDeviceAboutToStart(juce::AudioIODevice* device) {
    sampleRate_ = (int) device->getCurrentSampleRate();
    maxBlock_   = device->getCurrentBufferSizeSamples();
    engine_.prepare(sampleRate_, maxBlock_);
    if (onDeviceChanged) onDeviceChanged(sampleRate_, maxBlock_); // UI reloads model at new SR
}

void AudioDeviceCallbackAdapter::audioDeviceStopped() {}

void AudioDeviceCallbackAdapter::audioDeviceIOCallbackWithContext(
    const float* const* in, int numIn, float* const* out, int numOut,
    int numSamples, const juce::AudioIODeviceCallbackContext&) {
    const float* mono = (numIn > 0 && in[0] != nullptr) ? in[0] : nullptr;
    if (mono == nullptr) {                       // no input: output silence, stay safe
        for (int ch = 0; ch < numOut; ++ch)
            if (out[ch]) juce::FloatVectorOperations::clear(out[ch], numSamples);
        return;
    }
    float* dest = (numOut > 0 && out[0] != nullptr) ? out[0] : nullptr;
    if (dest == nullptr) return;
    engine_.render(mono, dest, numSamples);      // mono guitar -> channel 0
    for (int ch = 1; ch < numOut; ++ch)          // copy to remaining channels
        if (out[ch]) juce::FloatVectorOperations::copy(out[ch], dest, numSamples);
}
