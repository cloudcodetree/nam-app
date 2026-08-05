#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
#include "dsp/ToneEngine.h"
#include "net/Tone3000Api.h"
#include "app/ui/PlayScreen.h"
#include "app/ui/EditScreen.h"
#include "app/ui/RadioScreen.h"
#include "app/ui/PlaceholderScreen.h"

// Cross-platform app shell: owns every screen and swaps the visible one on
// navigation. Holds the shared dsp::ToneEngine so screens (Edit) can drive it.
// The Android/desktop/iOS shells each just host one AppShell.
class AppShell : public juce::Component {
public:
    explicit AppShell (dsp::ToneEngine& engine);

    void setLevels (float in, float out);   // forwarded to the Play meters

    // Inject the TONE3000 service (auth+search+download) the host owns, so the
    // Radio screen can search/keep without depending on the app-layer classes.
    using SearchFn   = std::function<void (juce::String query,
                             std::function<void (bool, std::vector<nam::ToneInfo>, juce::String)>)>;
    using DownloadFn = std::function<void (nam::ToneInfo,
                             std::function<void (bool, juce::String)>)>;
    void setTone3000 (SearchFn search, DownloadFn download);

    void resized() override;

private:
    enum class Screen { Play, Edit, Library, Radio, Live };
    void show (Screen s);

    dsp::ToneEngine& engine_;
    std::unique_ptr<PlayScreen>        play_;
    std::unique_ptr<EditScreen>        edit_;
    std::unique_ptr<RadioScreen>       radio_;
    std::unique_ptr<PlaceholderScreen> library_, live_;
    juce::Component* current_ = nullptr;

    SearchFn   searchFn_;
    DownloadFn downloadFn_;
    std::vector<nam::ToneInfo> radioResults_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AppShell)
};
