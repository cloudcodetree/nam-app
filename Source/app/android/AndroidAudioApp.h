#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <memory>
#include <vector>
#include "dsp/ToneEngine.h"
#include "model/NamModel.h"
#include "model/LibraryStore.h"
#include "app/Tone3000Auth.h"
#include "app/Tone3000Session.h"
#include "app/ui/NamLookAndFeel.h"
#include "app/ui/AppShell.h"

// Android app shell (Phase 5a): owns the audio device + ToneEngine + TONE3000
// service and hosts the SHARED, cross-platform Hi-Fi UI (Source/app/ui). Only
// this glue is Android-oriented; the screens are platform-agnostic JUCE.
class AndroidAudioApp : public juce::AudioAppComponent,
                        private juce::Timer,
                        private juce::ChangeListener {
public:
    AndroidAudioApp();
    ~AndroidAudioApp() override;

    // Audio-device picker support (exposed to the settings UI).
    juce::StringArray inputDeviceNames() const;
    juce::StringArray outputDeviceNames() const;
    juce::String currentInputDevice() const;
    juce::String currentOutputDevice() const;
    void selectInputDevice(const juce::String& name);
    void selectOutputDevice(const juce::String& name);
    void rescanAudioDevices();   // re-enumerate (JUCE's Oboe scan is frozen at launch)

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override;
    void releaseResources() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Android system back button: true if handled (navigated), false = exit.
    bool handleBackButton();

private:
    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    std::string copyBundledModelToFile();

    // Auto-route the guitar interface: if a USB input (iRig etc.) is present
    // and the user hasn't picked one manually, switch input to it.
    void preferUsbInput();
    juce::String applyDeviceSetup(juce::AudioDeviceManager::AudioDeviceSetup setup);

    // TONE3000: connect (refresh, else browser) -> search / download+import.
    void doSearch(juce::String query,
                  std::function<void(bool, std::vector<nam::ToneInfo>, juce::String)> done);
    void doDownload(nam::ToneInfo tone, std::function<void(bool, juce::String)> done);

    // Library: load a kept model file into the running engine.
    void loadModelEntry(const nam::LibraryEntry& e);

    static juce::File tokenStoreFile();
    static std::string defaultLibraryDir();
    static long long nowSeconds();

    nam::ui::NamLookAndFeel laf_;
    dsp::ToneEngine engine_;
    std::unique_ptr<AppShell> shell_;

    // TONE3000 (constructed before shell_ so setTone3000 can capture them).
    nam::LibraryStore library_ { defaultLibraryDir() };
    nam::Tone3000Auth t3kAuth_ { tokenStoreFile() };
    std::unique_ptr<nam::Tone3000Session> t3kSession_;

    double sampleRate_ = 48000.0;
    int    blockSize_  = 256;
    std::vector<float> mono_;
    std::atomic<float> inPeak_ { 0.0f };
    bool modelLoaded_ = false;

    bool applyingDeviceChange_ = false;  // re-entrancy guard for setAudioDeviceSetup
    bool userChoseInput_ = false;        // manual pick disables USB auto-select
    int  rescanTick_ = 0;                // slow hot-plug poll (timer runs at 30 Hz)
};
