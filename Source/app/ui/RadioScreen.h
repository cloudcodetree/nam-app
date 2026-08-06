#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
#include "net/Tone3000Api.h"

// The "Radio" screen: TONE3000 as a station. Opens onto the live TONE3000
// catalog (empty search = trending-style listing); station pills filter it.
// Tap a row to audition it (demo riff through that tone); ♥ keeps
// (downloads + imports). The list scrolls. Decoupled from app-layer auth via
// injected callbacks, so it stays cross-platform.
class RadioScreen : public juce::Component {
public:
    RadioScreen();

    std::function<void()>              onBack;
    std::function<void (juce::String)> onSearch;     // station/query -> external search
    std::function<void (int)>          onKeep;       // index into results -> download
    std::function<void (int)>          onAudition;   // index -> demo riff through tone

    void setResults (std::vector<nam::ToneInfo> results);
    void setStatus (juce::String s);
    void setPlaying (int index);   // -1 = none

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    std::vector<nam::ToneInfo> results_;
    juce::String status_ { "Tuning in to TONE3000" };
    int station_ = -1;
    int playing_ = -1;
    const juce::StringArray stations_ { "Blues", "Metal", "Clean", "Boutique" };

    // Fixed chrome; the result list scrolls (content-space rects + offset).
    juce::Rectangle<int> backRect_, stationRow_, list_, statusRect_;
    std::vector<juce::Rectangle<int>> stationRects_, rowRects_, keepRects_;
    float scrollY_ = 0.0f;
    int   contentH_ = 0;
    int   pressY_ = 0;
    float pressScroll_ = 0.0f;
    bool  dragged_ = false;

    void relayout();
    void clampScroll();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RadioScreen)
};
