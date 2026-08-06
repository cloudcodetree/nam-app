#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

// The "Devices" screen: pick the audio INPUT (guitar interface) and OUTPUT.
// The list scrolls (phones expose many outputs). Pure presentation — the
// owner supplies device names and applies selections.
class DevicesScreen : public juce::Component {
public:
    DevicesScreen();

    std::function<void()> onBack;
    std::function<void()> onRescan;                     // re-enumerate devices
    std::function<void (juce::String)> onSelectInput;
    std::function<void (juce::String)> onSelectOutput;

    void setDevices (juce::StringArray inputs,  juce::String currentInput,
                     juce::StringArray outputs, juce::String currentOutput);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::StringArray inputs_, outputs_;
    juce::String currentIn_, currentOut_;

    // Fixed top bar; everything below scrolls. Content rects are in content
    // space (y starts at 0) and get offset by contentTop - scrollY when drawn.
    juce::Rectangle<int> topBar_, backRect_, rescanRect_;
    juce::Rectangle<int> titleRect_, inHeader_, outHeader_;
    std::vector<juce::Rectangle<int>> inRects_, outRects_;
    int contentTop_ = 0, contentH_ = 0;
    float scrollY_ = 0.0f;

    // Drag-scroll state: a press is a tap unless it moves past a threshold.
    int  pressY_ = 0;
    float pressScroll_ = 0.0f;
    bool dragged_ = false;

    void relayout();
    void clampScroll();
    juce::Point<int> toContent (juce::Point<int> p) const;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DevicesScreen)
};
