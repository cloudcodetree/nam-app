#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
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
    void selectSampleRate(const juce::String& label);   // e.g. "48k"
    void selectBufferSize(const juce::String& label);   // e.g. "192"
    AudioSettingsState audioSettingsState();

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
    void doDownloadWithSession(nam::ToneInfo tone, std::function<void(bool, juce::String)> done);

    // Audition: download a tone's model (no library import), render the
    // selected dry demo riff through it offline, and loop the result.
    void doAudition(nam::ToneInfo tone, std::function<void(bool, juce::String)> done);
    void doAuditionModel(const std::string& toneId, const nam::ModelInfo& model,
                         std::function<void(bool, juce::String)> done);
    void doListModels(const std::string& toneId,
                      std::function<void(bool, std::vector<nam::ModelInfo>, juce::String)> done);
    void setDemoTrack(int index);         // 0 chords · 1 lead · 2 chugs
    void setDemoActive(bool on);
    void setLiveInputMuted(bool muted);   // browsing: don't amplify the mic
    void buildDemoLoop(double sampleRate);

    // Library: load a kept model file into the running engine.
    void loadModelEntry(const nam::LibraryEntry& e);

    // Runs `then(true)` with a valid token (refreshing silently if needed;
    // recreates the session so it carries the fresh token), else then(false).
    void withValidToken(std::function<void(bool)> then);

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
    std::string sessionToken_;   // token the session was built with (never logged)

    double sampleRate_ = 48000.0;
    int    blockSize_  = 256;
    std::vector<float> mono_;
    std::atomic<float> inPeak_ { 0.0f };
    bool modelLoaded_ = false;

    // Demo riff (audition mode). demoLoop_ is the DRY riff, built at prepare
    // time. An audition renders it through the tone's model OFFLINE on a
    // background thread into one of two slots; the audio thread just plays
    // the finished slot back (no inference on the audio thread — an emulated
    // CPU can't run NAM in real time). Slots are never freed: RT-safe.
    void installRenderedDemo(std::vector<float> rendered);
    void cacheAudition(const std::string& key, const std::vector<float>& rendered);
    const std::vector<float>* cachedAudition(const std::string& key) const;
    void renderAuditionFile(juce::File file, bool deleteAfter, std::string cacheKey,
                            juce::String displayName,
                            std::function<void(bool, juce::String)> done);
    // On-disk cache of downloaded audition model files: a demo-riff change
    // only re-renders, it never re-downloads.
    static juce::File modelCacheFile(const std::string& scope);
    static void pruneModelCache();
    std::array<std::vector<float>, 4> demoTracks_;   // chords / lead / chugs / bass
    int demoTrack_ = 0;
    // Rendered-audition cache (message thread only): tone id -> processed
    // riff. Re-tapping a tone plays instantly instead of re-downloading and
    // re-rendering. ~600 KB per entry; capped.
    std::vector<std::pair<std::string, std::vector<float>>> auditionCache_;
    std::array<std::vector<float>, 2> demoSlots_;
    std::atomic<int>    demoSlot_ { -1 };
    std::atomic<size_t> demoPos_ { 0 };
    std::atomic<bool>   demoOn_ { false };
    std::atomic<bool>   liveMuted_ { false };

    bool applyingDeviceChange_ = false;  // re-entrancy guard for setAudioDeviceSetup
    bool userChoseInput_ = false;        // manual pick disables USB auto-select
    int  rescanTick_ = 0;                // slow hot-plug poll (timer runs at 30 Hz)
};
