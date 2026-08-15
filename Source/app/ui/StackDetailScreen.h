#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "app/ui/StackEditView.h"
#include "app/ui/StackGearPicker.h"
#include "app/ui/StackItemSheet.h"
#include "app/ui/StackPerformView.h"
#include "model/StackModel.h"

// Stack detail: header (back / name / EDIT|PERFORM tabs) + body. EDIT hosts
// StackEditView (guided/freeform chain editor); the gear picker and item
// sheet overlays are owned here (not by StackEditView) so they paint over
// the header/tabs too, per the "overlays cover everything, painted last"
// house rule. PERFORM hosts StackPerformView, which is given the WHOLE
// component (not just the body) -- it's a full-bleed stage view with its
// own setlist header; none of this screen's own brand header/back/tab
// chrome is drawn or hit-tested while it's showing (see paint/mouseDown).
class StackDetailScreen : public juce::Component {
public:
    StackDetailScreen ();

    void setStack (const nam::Stack& stack, int idx, int count);   // count = setlist length
    void selectTab (bool perform);   // programmatic (Home's row vs PERFORM pill)
    int currentIndex () const { return idx_; }
    bool isPerformTab () const { return performTab_; }

    // Overlay accessors: the owner (AppShellStacks.cpp) wires network fetch
    // and model-mutation callbacks directly onto these, the same way it
    // wires stacksHome_/stacksDetail_'s own std::function members.
    StackGearPicker& picker () { return picker_; }
    StackItemSheet& itemSheet () { return itemSheet_; }
    StackPerformView& performView () { return performView_; }
    // Closes whichever overlay is frontmost (item sheet, then picker, then
    // EDIT's REMOVE STACK confirm) and reports whether one was open. The
    // owner's back-button chain consults this before popping Detail to
    // Home -- same "dismiss the overlay first" pattern the paywall sheet
    // already gets in AppShell::handleBackButton.
    bool closeTopOverlay ();

    std::function<void ()> onBack;
    std::function<void (bool)> onTabChanged;   // fires on any tab switch, user or programmatic
    std::function<void ()> onRemoveStack;      // EDIT's REMOVE STACK, after its confirm
    std::function<void (int, nam::Stack)> onChanged;   // EDIT's local mutations (freeform reorder)
    // "+ ADD" in a section, or freeform's "+ ADD GEAR" -> the owner computes
    // canAdd-based disabled tabs and opens picker() itself (it also owns
    // the AddChannel/Swap picker modes reached from itemSheet(), so all
    // "what does a picker pick mean" logic lives in one place).
    std::function<void (nam::GearType)> onAddGear;

    void paint (juce::Graphics&) override;
    void resized () override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void layout ();
    void wireChildren ();
    const nam::ChainItem* findItem (const juce::String& uid) const;

    nam::Stack stack_;
    int idx_ = -1;
    bool performTab_ = false;

    StackEditView editView_;
    StackGearPicker picker_;
    StackItemSheet itemSheet_;
    StackPerformView performView_;

    juce::Rectangle<int> backRect_, nameRect_, editTabRect_, performTabRect_, bodyRect_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StackDetailScreen)
};
