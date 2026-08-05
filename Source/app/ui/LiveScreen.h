#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
#include "model/LibraryEntry.h"

// The "Live" screen: big, sweat-proof setlist rows. Whole-row hit targets,
// active row highlighted. Tap to switch the live tone. No editing here (on
// purpose). Cross-platform (shared nam::LibraryEntry only).
class LiveScreen : public juce::Component {
public:
    LiveScreen();

    std::function<void()> onExit;
    std::function<void (nam::LibraryEntry)> onSelect;

    void setSlots (std::vector<nam::LibraryEntry> slots);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    std::vector<nam::LibraryEntry> slots_;
    int active_ = 0;
    juce::Rectangle<int> exitRect_;
    std::vector<juce::Rectangle<int>> rowRects_;
    void relayout();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveScreen)
};
