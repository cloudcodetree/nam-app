#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
#include "dsp/ToneEngine.h"
#include "net/Tone3000Api.h"
#include "model/LibraryEntry.h"
#include "app/ui/PlayScreen.h"
#include "app/ui/EditScreen.h"
#include "app/ui/RadioScreen.h"
#include "app/ui/LibraryScreen.h"
#include "app/ui/LiveScreen.h"
#include "app/ui/SetupScreen.h"
#include "app/ui/DevicesScreen.h"

// Cross-platform app shell: owns every screen and swaps the visible one on
// navigation. Holds the shared dsp::ToneEngine so screens drive it. The
// Android/desktop/iOS shells each just host one AppShell.
class AppShell : public juce::Component {
public:
    explicit AppShell (dsp::ToneEngine& engine);

    void setLevels (float in, float out);     // Play meters + Setup listener
    void startOnSetup();                       // first-launch entry point

    // Android system back: pop to Play if on a sub-screen. Returns true if it
    // handled the press (caller should NOT exit the app); false if already on
    // Play (let the OS do the default = leave the app).
    bool handleBackButton();

    // TONE3000 service (Radio search/download), owned by the host.
    using SearchFn   = std::function<void (juce::String,
                             std::function<void (bool, std::vector<nam::ToneInfo>, juce::String)>)>;
    using DownloadFn = std::function<void (nam::ToneInfo,
                             std::function<void (bool, juce::String)>)>;
    void setTone3000 (SearchFn search, DownloadFn download);

    // Audition service: play the built-in demo riff through a tone's model
    // (audition) and stop it (leaving the Radio screen stops automatically).
    using AuditionFn = std::function<void (nam::ToneInfo,
                             std::function<void (bool, juce::String)>)>;
    void setAuditionService (AuditionFn audition, std::function<void()> stopDemo);

    // Library service: list kept models + load one into the engine.
    using GetModelsFn = std::function<std::vector<nam::LibraryEntry>()>;
    using LoadModelFn = std::function<void (nam::LibraryEntry)>;
    void setLibraryService (GetModelsFn getModels, LoadModelFn loadModel);

    // Audio device service: enumerate inputs/outputs and apply a selection.
    struct AudioDeviceState {
        juce::StringArray inputs, outputs;
        juce::String currentInput, currentOutput;
    };
    using GetDevicesFn   = std::function<AudioDeviceState()>;
    using SelectDeviceFn = std::function<void (juce::String)>;
    using RescanFn       = std::function<void()>;
    void setAudioDeviceService (GetDevicesFn get, SelectDeviceFn selectInput,
                                SelectDeviceFn selectOutput, RescanFn rescan = {});

    void resized() override;

private:
    enum class Screen { Play, Edit, Library, Radio, Live, Setup, Devices };
    void show (Screen s);
    void refreshDevices();
    void runRadioSearch (juce::String query);

    dsp::ToneEngine& engine_;
    std::unique_ptr<PlayScreen>    play_;
    std::unique_ptr<EditScreen>    edit_;
    std::unique_ptr<RadioScreen>   radio_;
    std::unique_ptr<LibraryScreen> library_;
    std::unique_ptr<LiveScreen>    live_;
    std::unique_ptr<SetupScreen>   setup_;
    std::unique_ptr<DevicesScreen> devices_;
    juce::Component* current_ = nullptr;

    SearchFn    searchFn_;
    DownloadFn  downloadFn_;
    AuditionFn  auditionFn_;
    std::function<void()> stopDemoFn_;
    int  auditioning_ = -1;
    bool radioLoadedOnce_ = false;
    GetModelsFn getModels_;
    LoadModelFn loadModel_;
    GetDevicesFn   getDevices_;
    SelectDeviceFn selectInput_, selectOutput_;
    RescanFn       rescanDevices_;
    std::vector<nam::ToneInfo> radioResults_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AppShell)
};
