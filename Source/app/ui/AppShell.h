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

// Cross-platform app shell: owns every screen and swaps the visible one on
// navigation. Holds the shared dsp::ToneEngine so screens drive it. The
// Android/desktop/iOS shells each just host one AppShell.
class AppShell : public juce::Component {
public:
    explicit AppShell (dsp::ToneEngine& engine);

    void setLevels (float in, float out);     // Play meters + Setup listener
    void startOnSetup();                       // first-launch entry point

    // TONE3000 service (Radio search/download), owned by the host.
    using SearchFn   = std::function<void (juce::String,
                             std::function<void (bool, std::vector<nam::ToneInfo>, juce::String)>)>;
    using DownloadFn = std::function<void (nam::ToneInfo,
                             std::function<void (bool, juce::String)>)>;
    void setTone3000 (SearchFn search, DownloadFn download);

    // Library service: list kept models + load one into the engine.
    using GetModelsFn = std::function<std::vector<nam::LibraryEntry>()>;
    using LoadModelFn = std::function<void (nam::LibraryEntry)>;
    void setLibraryService (GetModelsFn getModels, LoadModelFn loadModel);

    void resized() override;

private:
    enum class Screen { Play, Edit, Library, Radio, Live, Setup };
    void show (Screen s);

    dsp::ToneEngine& engine_;
    std::unique_ptr<PlayScreen>    play_;
    std::unique_ptr<EditScreen>    edit_;
    std::unique_ptr<RadioScreen>   radio_;
    std::unique_ptr<LibraryScreen> library_;
    std::unique_ptr<LiveScreen>    live_;
    std::unique_ptr<SetupScreen>   setup_;
    juce::Component* current_ = nullptr;

    SearchFn    searchFn_;
    DownloadFn  downloadFn_;
    GetModelsFn getModels_;
    LoadModelFn loadModel_;
    std::vector<nam::ToneInfo> radioResults_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AppShell)
};
