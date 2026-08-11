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
#include "app/SearchPanel.h"
#include "app/Tone3000Auth.h"
#include "app/Tone3000Session.h"

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
    void browseT3kButtonClicked();
    void doTone3000Search(const juce::String& query);
    void downloadPickedTone(const nam::ToneInfo& tone);
    void timerCallback() override;   // repaint meter + latency + telemetry
    void reloadCurrentModelAt(int sampleRate, int maxBlock);
    void reloadCurrentIrAt(int sampleRate);
    void handleLibraryEntryLoad(const nam::LibraryEntry& e);   // libraryPanel_.onLoadEntry
    static std::string defaultLibraryDir();
    static long long nowSeconds();   // std::chrono::system_clock, app layer only

    juce::AudioDeviceManager deviceManager_;
    dsp::ToneEngine engine_;
    AudioDeviceCallbackAdapter adapter_{ engine_ };
    nam::ModelHost host_;

    juce::AudioDeviceSelectorComponent selector_{ deviceManager_, 1,     2,    1,    2,
                                                  false,          false, true, false };
    juce::TextButton loadButton_{ "Load .nam model" };
    juce::Label modelLabel_{ {}, "No model loaded" };
    juce::Slider inGain_, outGain_;
    juce::Label latencyLabel_;
    juce::Label statsLabel_;   // peak / load / blocks / xruns
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::String currentModelPath_;

    // Noise gate.
    juce::ToggleButton gateEnable_;
    juce::Slider gateThresh_;

    // 3-band EQ.
    juce::ToggleButton eqEnable_;
    juce::Slider eqLow_, eqMid_, eqHigh_;

    // Delay (time FX): time / feedback / mix.
    juce::ToggleButton delayEnable_;
    juce::Slider delayTime_, delayFeedback_, delayMix_;

    // Reverb (time FX): room size / damping / mix.
    juce::ToggleButton reverbEnable_;
    juce::Slider reverbRoom_, reverbDamp_, reverbMix_;

    // IR cab.
    juce::ToggleButton irEnable_;
    juce::TextButton loadIrButton_{ "Load IR (.wav)" };
    juce::Label irLabel_{ {}, "No IR" };
    std::unique_ptr<juce::FileChooser> irChooser_;
    juce::String currentIrPath_;

    // Local library of imported/favorited models + IRs. Constructed before
    // libraryPanel_ (declaration order), which holds a reference to it.
    nam::LibraryStore library_{ defaultLibraryDir() };
    LibraryPanel libraryPanel_{ library_ };

    // TONE3000 browse/download: OAuth2 PKCE auth + authenticated model
    // download, wired to the "Browse TONE3000" button below.
    nam::Tone3000Auth t3kAuth_{ juce::File::getSpecialLocation(
                                    juce::File::userApplicationDataDirectory)
                                    .getChildFile("NAM Player/tone3000_tokens.json") };
    std::unique_ptr<nam::Tone3000Session> t3kSession_;
    juce::TextButton browseT3kButton_{ "Browse TONE3000" };
    juce::Label t3kStatus_;

    // In-app TONE3000 search panel: query -> results -> pick -> download
    // (reuses t3kAuth_/t3kSession_ and the same download path as
    // browseT3kButtonClicked() above).
    SearchPanel searchPanel_;

    // UI-thread meter state (read from engine telemetry each timer tick).
    juce::Rectangle<int> meterBounds_;
    float meterDb_ = -100.0f;   // smoothed/decayed for display

#if JUCE_DEBUG
    // Melatonin Inspector: DevTools-style live component-tree inspector.
    // Debug builds only. Toggle with the "Inspect UI" button.
    melatonin::Inspector inspector_{ *this, false };
    juce::TextButton inspectButton_{ "Inspect UI" };
#endif
};
