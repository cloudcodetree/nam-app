#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "dsp/ToneEngine.h"
#include "model/ModelHost.h"
#include "app/AudioDeviceCallbackAdapter.h"

#if JUCE_DEBUG
#include "melatonin_inspector/melatonin_inspector.h"
#endif

class MainComponent : public juce::Component, private juce::Timer {
public:
    MainComponent();
    ~MainComponent() override;
    void resized() override;
    void paint(juce::Graphics&) override;
private:
    void loadButtonClicked();
    void timerCallback() override;          // repaint meter + latency + telemetry
    void reloadCurrentModelAt(int sampleRate, int maxBlock);

    juce::AudioDeviceManager deviceManager_;
    dsp::ToneEngine engine_;
    AudioDeviceCallbackAdapter adapter_{engine_};
    nam::ModelHost host_;

    juce::AudioDeviceSelectorComponent selector_
        { deviceManager_, 1, 2, 1, 2, false, false, true, false };
    juce::TextButton loadButton_ { "Load .nam model" };
    juce::Label      modelLabel_  { {}, "No model loaded" };
    juce::Slider     inGain_, outGain_;
    juce::Label      latencyLabel_;
    juce::Label      statsLabel_;             // peak / load / blocks / xruns
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::String currentModelPath_;

    // UI-thread meter state (read from engine telemetry each timer tick).
    juce::Rectangle<int> meterBounds_;
    float meterDb_ = -100.0f;                 // smoothed/decayed for display

#if JUCE_DEBUG
    // Melatonin Inspector: DevTools-style live component-tree inspector.
    // Debug builds only. Toggle with the "Inspect UI" button.
    melatonin::Inspector inspector_ { *this, false };
    juce::TextButton inspectButton_ { "Inspect UI" };
#endif
};
