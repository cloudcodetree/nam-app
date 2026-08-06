#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

// The "Devices" screen: pick the audio INPUT (guitar interface) and OUTPUT.
// Pure presentation — the owner supplies device names and applies selections.
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

private:
    juce::StringArray inputs_, outputs_;
    juce::String currentIn_, currentOut_;

    juce::Rectangle<int> backRect_, rescanRect_, titleRect_;
    std::vector<juce::Rectangle<int>> inRects_, outRects_;
    juce::Rectangle<int> inHeader_, outHeader_;

    void relayout();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DevicesScreen)
};
