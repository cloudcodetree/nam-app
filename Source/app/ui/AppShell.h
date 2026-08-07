#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>
#include <vector>
#include "dsp/ToneEngine.h"
#include "net/Tone3000Api.h"
#include "model/LibraryEntry.h"
#include "app/ui/PlayScreen.h"
#include "app/ui/EditScreen.h"
#include "app/ui/BrowseScreen.h"
#include "app/ui/LibraryScreen.h"
#include "app/ui/LiveScreen.h"
#include "app/ui/AudioSettingsScreen.h"
#include "app/ui/TunerScreen.h"

// Cross-platform app shell: owns every screen and swaps the visible one on
// navigation. Holds the shared dsp::ToneEngine so screens drive it. The
// Android/desktop/iOS shells each just host one AppShell.
class AppShell : public juce::Component {
public:
    explicit AppShell (dsp::ToneEngine& engine);

    void setLevels (float in, float out);     // Play + Audio settings meters
    void setTunerPitch (float hz);            // 0 = no pitch detected
    void setLatencyMs (double ms);            // round-trip; shown in the chrome
    void showEntryAsNowPlaying (const nam::LibraryEntry& e);   // reflect a loaded tone

    // Android system back: pop to Play if on a sub-screen. Returns true if it
    // handled the press (caller should NOT exit the app); false if already on
    // Play (let the OS do the default = leave the app).
    bool handleBackButton();

    // TONE3000 / Browse services, owned by the host. Auditioning renders the
    // demo riff through a model; leaving Browse stops the demo automatically.
    using DoneFn     = std::function<void (bool, juce::String)>;
    using SearchFn   = std::function<void (juce::String,
                             std::function<void (bool, std::vector<nam::ToneInfo>, juce::String)>)>;
    using DownloadFn = std::function<void (nam::ToneInfo, DoneFn)>;
    using AuditionFn = std::function<void (nam::ToneInfo, DoneFn)>;
    struct BrowseServices {
        SearchFn   search;
        DownloadFn keep;           // favorite: import the downloaded model
        DownloadFn downloadOnly;   // fetch best quality locally, no play/import
        AuditionFn audition;                                   // auto variant
        std::function<void (std::string, nam::ModelInfo, bool /*isIr*/, DoneFn)> auditionModel;
        std::function<void (bool)> muteLiveInput;   // Browse mutes the guitar path
        std::function<void (std::string,
            std::function<void (bool, std::vector<nam::ModelInfo>, juce::String)>)> listModels;
        // Async: may need to fetch the DI track first (full TONE3000 catalog).
        std::function<void (int, std::function<void (bool)>)> setDemoTrack;
        std::function<void (int)>  setCab;    // cab IR index (0 = none)
        std::function<void()>      stopDemo;
        std::function<bool (std::string)> isAuditionCached;   // toneId, current riff
        std::function<bool (std::string)> isDownloaded;       // best-quality on disk
    };
    void setBrowseServices (BrowseServices services);

    // Host reports offline-render progress (0..1) for the in-flight audition.
    void setAuditionProgress (float progress);

    // Library service: list kept models + load one into the engine.
    using GetModelsFn = std::function<std::vector<nam::LibraryEntry>()>;
    using LoadModelFn = std::function<void (nam::LibraryEntry)>;
    void setLibraryService (GetModelsFn getModels, LoadModelFn loadModel);

    // Audio settings service: enumerate devices/rates/buffers, apply picks.
    using GetDevicesFn   = std::function<AudioSettingsState()>;
    using SelectDeviceFn = std::function<void (juce::String)>;
    using RescanFn       = std::function<void()>;
    void setAudioDeviceService (GetDevicesFn get, SelectDeviceFn selectInput,
                                SelectDeviceFn selectOutput, RescanFn rescan = {},
                                SelectDeviceFn selectRate = {},
                                SelectDeviceFn selectBuffer = {});

    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    enum class Screen { Play, Edit, Library, Browse, Live, Devices, Tuner };
    void show (Screen s);
    void refreshDevices();
    void runBrowseSearch (juce::String query);
    void stopAudition();
    void refreshCachedFlags();
    void stepCollection (int delta);          // Play ‹ › through the Library
    juce::Rectangle<int> contentBounds() const;   // above the global chrome

    dsp::ToneEngine& engine_;
    std::unique_ptr<PlayScreen>    play_;
    std::unique_ptr<EditScreen>    edit_;
    std::unique_ptr<BrowseScreen>  browse_;
    std::unique_ptr<LibraryScreen> library_;
    std::unique_ptr<LiveScreen>    live_;
    std::unique_ptr<AudioSettingsScreen> devices_;
    std::unique_ptr<TunerScreen>   tuner_;
    juce::Component* current_ = nullptr;

    BrowseServices svc_;
    int  auditioningPack_ = -1, auditioningModel_ = -1;
    bool browseLoadedOnce_ = false;
    int  collectionIndex_ = -1;               // Play screen position in the Library

    // Global bottom chrome: slim input meter strip + persistent nav bar.
    juce::Rectangle<int> navBar_, meterBar_;
    std::array<juce::Rectangle<int>, 4> navRects_;
    int   activeTab_ = 0;                     // 0 Play · 1 Edit · 2 Radio · 3 Live (-1 none)
    float meterInPeak_ = 0.0f;
    double latencyMs_ = 0.0;
    GetModelsFn getModels_;
    LoadModelFn loadModel_;
    GetDevicesFn   getDevices_;
    SelectDeviceFn selectInput_, selectOutput_, selectRate_, selectBuffer_;
    RescanFn       rescanDevices_;
    std::vector<nam::ToneInfo> browseResults_;
    std::vector<std::vector<nam::ModelInfo>> browseModels_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AppShell)
};
