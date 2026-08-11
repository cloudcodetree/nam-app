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
#include "app/ui/DemoTrackCatalog.h"

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
    // Live guitar wants the smallest buffer the device offers; JUCE's Oboe
    // default (1920 frames with a USB interface = 40 ms) is a media default.
    void clampBufferForLowLatency();
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
    void doSearchEx(nam::SearchParams params,
                    std::function<void(bool, std::vector<nam::ToneInfo>, juce::String)> done);
    void doDownload(nam::ToneInfo tone, std::function<void(bool, juce::String)> done);
    void doDownloadOnly(nam::ToneInfo tone, std::function<void(bool, juce::String)> done);
    // Star toggle: in the deck -> remove from the Library; not in -> keep it.
    void doToggleKeep(nam::ToneInfo tone, std::function<void(bool, juce::String)> done);
    // Empty deck: fetch TONE3000's most-downloaded A2 tone as the default.
    void fetchPopularDefault();
    static juce::File defaultToneFile();
    static juce::File defaultToneNameFile();
    bool defaultFetchKicked_ = false;
    // Library id of the entry imported for this tone ("" if not kept).
    std::string libraryIdForTone(const std::string& toneId) const;
    // TONE3000 artwork: cached on disk per tone, shown as Play "album art".
    static juce::File artworkFile(const std::string& toneId);
    static std::string toneIdFromEntry(const nam::LibraryEntry& e);
    void fetchArtwork(nam::ToneInfo tone);   // async download+downscale (no-op if cached)
    void auditionFromFile(juce::File file, bool deleteAfter, const std::string& cacheKey,
                          juce::String displayName,
                          std::function<void(bool, juce::String)> done,
                          std::shared_ptr<const std::vector<float>> overrideIr = nullptr);
    // IR tones: audition = load the .wav as the cab impulse under the current
    // amp model (the demo keeps rolling — gapless cab A/B).
    void doAuditionIr(nam::ToneInfo tone, std::function<void(bool, juce::String)> done);

    // Audition: download a tone's model (no library import), render the
    // selected dry demo riff through it offline, and loop the result.
    void doAudition(nam::ToneInfo tone, std::function<void(bool, juce::String)> done);
    void doAuditionModel(const std::string& toneId, const nam::ModelInfo& model, bool isIr,
                         std::function<void(bool, juce::String)> done);
    void applyIrAudition(juce::File irWav, const std::string& cacheKey,
                         juce::String displayName,
                         std::function<void(bool, juce::String)> done);
    void doListModels(const std::string& toneId,
                      std::function<void(bool, std::vector<nam::ModelInfo>, juce::String)> done);
    void setDemoTrack(int index);         // index into nam::demo::kTracks
    void setCab(int index);               // index into nam::demo::kCabs (0 = none)
    // Makes sure a DI track's audio is in memory (bundled, disk cache, or
    // downloaded from TONE3000's repo), then done(true) on the msg thread.
    void ensureDemoTrack(int index, std::function<void(bool)> done);
    void setDemoActive(bool on);
    void setLiveInputMuted(bool muted);   // browsing: don't amplify the mic
    void buildDemoLoop(double sampleRate);

    // Library: load a kept model file into the running engine.
    void loadModelEntry(const nam::LibraryEntry& e);

    // Stacks: load a tone (model or cab impulse, by format) straight into
    // the engine, downloading on demand if it isn't cached yet.
    void doLoadToneLive(nam::ToneInfo tone, std::function<void(bool, juce::String)> done);
    static juce::File stacksFile();   // stacks.json under appdata

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
    std::atomic<float> outPeak_ { 0.0f };   // metered at the device write, not the engine
    bool modelLoaded_ = false;

    // Demo riff (audition mode). demoLoop_ is the DRY riff, built at prepare
    // time. An audition renders it through the tone's model OFFLINE on a
    // background thread into one of two slots; the audio thread just plays
    // the finished slot back (no inference on the audio thread — an emulated
    // CPU can't run NAM in real time). Slots are never freed: RT-safe.
    void installRenderedDemo(std::vector<float> rendered, bool preservePosition);
    void cacheAudition(const std::string& key, const std::vector<float>& rendered);
    const std::vector<float>* cachedAudition(const std::string& key) const;
    // On-disk cache of downloaded audition model files: a demo-riff change
    // only re-renders, it never re-downloads.
    static juce::File modelCacheFile(const std::string& scope);
    static void pruneModelCache();
    // One slot per catalog track; fixed size (audio thread indexes into it).
    // Buffers are IMMUTABLE once published: writers publish a NEW shared_ptr
    // (message thread) and the audio thread takes its own reference at block
    // start, so a re-publish can never mutate a vector mid-read (the race
    // the adversarial review flagged). Same pattern as cabIrs_.
    std::array<std::shared_ptr<const std::vector<float>>,
               (size_t) nam::demo::kNumTracks> demoTracks_;
    std::array<bool, (size_t) nam::demo::kNumTracks> demoFetching_ {};   // msg thread
    int demoTrack_ = 0;
    // Bundled cab IRs (loaded at prepare; shared_ptr swap is RT-safe).
    std::array<std::shared_ptr<const std::vector<float>>,
               (size_t) nam::demo::kNumCabs> cabIrs_;
    int cab_ = 0;
    std::atomic<int>  demoTrackRT_ { 0 };   // audio-thread copy of demoTrack_
    std::atomic<bool> demoLive_ { false };  // live mode: dry DI -> engine in RT
    bool preRenderAuditions_ = false;       // emulator: can't run NAM in RT
    // Rendered-audition cache (message thread only): tone id -> processed
    // riff. Re-tapping a tone plays instantly instead of re-downloading and
    // re-rendering. ~600 KB per entry; capped.
    std::vector<std::pair<std::string, std::vector<float>>> auditionCache_;
    // Rendered-audition slots: published as immutable shared_ptrs so an
    // audio block holding a reference survives back-to-back installs that
    // overwrite both slots (the two-slot flip alone was not enough).
    std::array<std::shared_ptr<const std::vector<float>>, 2> demoSlots_;
    std::atomic<int>    demoSlot_ { -1 };
    std::atomic<size_t> demoPos_ { 0 };
    std::atomic<bool>   demoOn_ { false };
    std::atomic<bool>   liveMuted_ { false };
    bool alwaysMuteLive_ = false;   // emulator: synthetic mic, never unmute
    // Feedback guard: with no USB interface the live path is phone mic ->
    // amp sim -> speaker = howl. Mutes the OUTPUT automatically (input
    // meter/tuner stay live); manual device picks in SETUP override.
    std::atomic<bool> feedbackGuard_ { false };
    void updateFeedbackGuard();
    // Status-orb toggles (UI-driven, independent of the guard).
    std::atomic<bool> inputMutedUser_ { false };
    std::atomic<bool> outputMutedUser_ { false };

    // Tuner: the audio thread mirrors the RAW live input into a ring; the
    // UI timer analyzes the latest window (guitar only — demos don't tune).
    static constexpr int kTunerRingSize = 4096;   // power of two
    std::array<float, (size_t) kTunerRingSize> tunerRing_ {};
    std::atomic<int> tunerWrite_ { 0 };
    int tunerTick_ = 0;

    bool applyingDeviceChange_ = false;  // re-entrancy guard for setAudioDeviceSetup
    bool userChoseInput_ = false;        // manual pick disables USB auto-select
    bool userChoseOutput_ = false;       // manual pick disables USB output auto-claim
    bool userChoseBuffer_ = false;       // manual pick disables the low-latency clamp
    int  rescanTick_ = 0;                // slow hot-plug poll (timer runs at 30 Hz)
    int  engineDeadTicks_ = 0;           // watchdog: device stopped this many ticks
};
