#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
#include "model/LibraryEntry.h"

// The "Library" screen: a 2-column artwork grid of kept tones from the real
// LibraryStore. Tap a card to load that model into the engine. All/Favorites
// filter. Cross-platform (depends only on the shared nam::LibraryEntry).
class LibraryScreen : public juce::Component {
public:
    LibraryScreen();

    std::function<void()> onBack;
    std::function<void (nam::LibraryEntry)> onLoad;

    void setEntries (std::vector<nam::LibraryEntry> entries);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    std::vector<nam::LibraryEntry> all_, shown_;
    int filter_ = 0;   // 0 = All, 1 = Favorites
    juce::Rectangle<int> backRect_, filterRow_;
    std::vector<juce::Rectangle<int>> filterRects_, cardRects_;

    void applyFilter();
    void relayout();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibraryScreen)
};
