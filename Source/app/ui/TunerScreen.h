#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>

// Full-screen tuner with selectable displays:
//   STROBE — three bands (1x/2x/4x); drift speed/direction = deviation
//   NEEDLE — analog dial, needle sweeps -50..+50 cents
//   BARS   — pedal-style LED segments
// The owner feeds raw pitch (Hz) from the detector; this screen derives
// note/cents and smooths them for display.
class TunerScreen : public juce::Component, private juce::Timer {
public:
    TunerScreen ();
    ~TunerScreen () override;

    std::function<void ()> onBack;

    void setPitch (float hz);   // 0 = no pitch

    // Panel mode: rendered as a rounded card growing out of the Play tuner
    // panel (no BACK pill / hero background; the panel below collapses it).
    void setPanelMode (bool on);
    // Height the panel-mode card wants (content-sized, not full screen).
    static constexpr int kPanelHeight = 470;

    void paint (juce::Graphics&) override;
    void resized () override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback () override;
    void paintStrobe (juce::Graphics&, bool inTune);
    void paintNeedle (juce::Graphics&, bool inTune);
    void paintBars (juce::Graphics&, bool inTune);

    enum class Mode { Strobe, Needle, Bars };
    Mode mode_ = Mode::Strobe;
    bool panelMode_ = false;
    std::array<juce::Rectangle<int>, 3> modeRects_;

    float hz_ = 0.0f;
    float cents_ = 0.0f;   // smoothed deviation
    juce::String note_;
    bool active_ = false;
    int inactiveTicks_ = 0;
    double phase_ = 0.0;   // strobe scroll phase (px)

    juce::Rectangle<int> backRect_, modeRow_, noteRect_, centsRect_, hzRect_, bandsRect_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TunerScreen)
};
