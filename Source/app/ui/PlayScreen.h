#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
#include <array>

// The "Play" screen (resting state) from the Hi-Fi design: hero tone card,
// transport, tuner + live IN/OUT meters, and the PLAY/EDIT/RADIO/LIVE nav bar.
// Pure presentation — the owner feeds live meter levels and handles nav.
class PlayScreen : public juce::Component {
public:
    PlayScreen();

    // Live meter levels, 0..1 (owner's timer pushes these from engine telemetry).
    void setLevels (float in, float out);

    std::function<void (int)> onNav;      // 0=Play 1=Edit 2=Radio 3=Live
    std::function<void()>     onLibrary;
    std::function<void()>     onSettings; // I/O pill -> audio device picker

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    struct Tone { juce::String name, family, author; };
    std::vector<Tone> tones_;
    int   index_    = 0;
    float inLevel_  = 0.58f;
    float outLevel_ = 0.0f;

    // Hit / layout rects, computed in resized().
    juce::Rectangle<int> topBar_, hero_, artRect_, textRect_, transportRect_,
                         metersRow_, navBar_;
    juce::Rectangle<int> libRect_, ioRect_, prevRect_, nextRect_, progressRect_;
    std::array<juce::Rectangle<int>, 4> navRects_;

    void layout();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlayScreen)
};
