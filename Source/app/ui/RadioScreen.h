#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
#include "net/Tone3000Api.h"

// The "Radio" screen: TONE3000 as a station. Genre pills run a real TONE3000
// search; results render as a track list; ♥ keeps (downloads + imports). The
// screen is decoupled from the app-layer auth via injected callbacks, so it
// stays cross-platform (depends only on the shared nam::ToneInfo).
class RadioScreen : public juce::Component {
public:
    RadioScreen();

    std::function<void()>            onBack;
    std::function<void (juce::String)> onSearch;   // station/query -> external search
    std::function<void (int)>        onKeep;       // index into results -> download

    void setResults (std::vector<nam::ToneInfo> results);
    void setStatus (juce::String s);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    std::vector<nam::ToneInfo> results_;
    juce::String status_ { "Tap a station to connect TONE3000" };
    int station_ = -1;
    const juce::StringArray stations_ { "Blues", "Metal", "Clean", "Boutique" };

    juce::Rectangle<int> backRect_, stationRow_, list_, statusRect_;
    std::vector<juce::Rectangle<int>> stationRects_, rowRects_, keepRects_;

    void relayout();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RadioScreen)
};
