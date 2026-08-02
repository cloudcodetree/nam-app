#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "dsp/ToneEngine.h"
#include "dsp/IrCab.h"
#include "model/ModelHost.h"
#include "model/IrLoader.h"
#include "model/LibraryStore.h"
#include "model/LibraryEntry.h"
#include "app/AudioDeviceCallbackAdapter.h"
#include "app/LibraryPanel.h"

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
    void loadIrClicked();
    void timerCallback() override;          // repaint meter + latency + telemetry
    void reloadCurrentModelAt(int sampleRate, int maxBlock);
    void reloadCurrentIrAt(int sampleRate);
    void handleLibraryEntryLoad(const nam::LibraryEntry& e);  // libraryPanel_.onLoadEntry
    static std::string defaultLibraryDir();
    static long long nowSeconds();          // std::chrono::system_clock, app layer only

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

    // Noise gate.
    juce::ToggleButton gateEnable_;
    juce::Slider       gateThresh_;

    // 3-band EQ.
    juce::ToggleButton eqEnable_;
    juce::Slider       eqLow_, eqMid_, eqHigh_;

    // IR cab.
    juce::ToggleButton irEnable_;
    juce::TextButton   loadIrButton_ { "Load IR (.wav)" };
    juce::Label        irLabel_ { {}, "No IR" };
    std::unique_ptr<juce::FileChooser> irChooser_;
    juce::String currentIrPath_;

    // Local library of imported/favorited models + IRs. Constructed before
    // libraryPanel_ (declaration order), which holds a reference to it.
    nam::LibraryStore library_ { defaultLibraryDir() };
    LibraryPanel libraryPanel_ { library_ };

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
