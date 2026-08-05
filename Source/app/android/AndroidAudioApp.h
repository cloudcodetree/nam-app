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
                        private juce::Timer {
public:
    AndroidAudioApp();
    ~AndroidAudioApp() override;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override;
    void releaseResources() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Android system back button: true if handled (navigated), false = exit.
    bool handleBackButton();

private:
    void timerCallback() override;
    std::string copyBundledModelToFile();

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
};
