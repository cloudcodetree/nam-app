#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// Full-screen strobe tuner: big detected note + cents, and three strobe
// bands (1x / 2x / 4x rates). Band drift speed and direction follow the
// deviation — stationary bands = in tune, drift right = sharp, left = flat.
// The owner feeds raw pitch (Hz) from the detector; this screen derives
// note/cents and smooths them for display.
class TunerScreen : public juce::Component, private juce::Timer {
public:
    TunerScreen();
    ~TunerScreen() override;

    std::function<void()> onBack;

    void setPitch (float hz);   // 0 = no pitch

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    float hz_ = 0.0f;
    float cents_ = 0.0f;        // smoothed deviation
    juce::String note_;
    bool active_ = false;
    int  inactiveTicks_ = 0;
    double phase_ = 0.0;        // strobe scroll phase (px)

    juce::Rectangle<int> backRect_, noteRect_, centsRect_, hzRect_, bandsRect_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TunerScreen)
};
