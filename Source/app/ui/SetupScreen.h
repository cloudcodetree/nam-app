#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// The "Setup" (first-launch) screen: auto gain-staging. Animated listening
// bars while it watches the real input level, then "You're set" once enough
// signal arrives; it sets the input gain for headroom. Cross-platform JUCE.
class SetupScreen : public juce::Component, private juce::Timer {
public:
    SetupScreen();
    ~SetupScreen() override;

    void setLevel (float inPeak);                  // owner pushes live input peak
    std::function<void (float dbGain)> onSetInputDb;  // apply the computed gain
    std::function<void()> onFinish;                // continue -> Play

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    bool  listening_ = true;
    float level_ = 0.0f;      // smoothed display level 0..1
    float maxPeak_ = 0.0f;    // running max during listening
    int   ticks_ = 0;
    float appliedDb_ = 0.0f;
    juce::Rectangle<int> primaryBtn_, secondaryBtn_;
    void finishListening();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SetupScreen)
};
