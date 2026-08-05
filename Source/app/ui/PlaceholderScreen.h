#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// Temporary on-brand placeholder for screens not yet built (Library/Radio/
// Live/Setup). Keeps navigation complete and visually consistent while each
// real screen is filled in. Back arrow returns to Play.
class PlaceholderScreen : public juce::Component {
public:
    explicit PlaceholderScreen (juce::String title);
    std::function<void()> onBack;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
private:
    juce::String title_;
    juce::Rectangle<int> backRect_;
};
