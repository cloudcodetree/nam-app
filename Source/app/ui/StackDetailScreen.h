#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "model/StackModel.h"

// Stack detail: header (back / name / EDIT|PERFORM tabs) + body. This task
// ships the SHELL only -- the body is a placeholder; EDIT's gear chain and
// PERFORM's scene/stomp surface land in later tasks. Presentation only.
class StackDetailScreen : public juce::Component {
public:
    StackDetailScreen ();

    void setStack (const nam::Stack& stack, int idx);
    void selectTab (bool perform);   // programmatic (Home's row vs PERFORM pill)

    std::function<void ()> onBack;
    std::function<void (bool)> onTabChanged;   // fires on any tab switch, user or programmatic
    std::function<void ()> onSettings;         // gear icon -> orb I/O flyout

    void paint (juce::Graphics&) override;
    void resized () override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void layout ();

    nam::Stack stack_;
    int idx_ = -1;
    bool performTab_ = false;

    juce::Rectangle<int> headerRect_, gearRect_, backRect_, nameRect_, editTabRect_,
        performTabRect_, bodyRect_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StackDetailScreen)
};
