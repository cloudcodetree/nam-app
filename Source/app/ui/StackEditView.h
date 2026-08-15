#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <vector>
#include "model/StackModel.h"

// Stack detail EDIT tab content: ROUTING pills + FREEFORM toggle, guided
// sections (PEDALS/AMP/POST, each with "+ ADD") or the flat freeform
// reorderable list, and a REMOVE STACK row with an inline confirm. Pure
// content -- the shared gear-picker/item-sheet overlays are owned by
// StackDetailScreen (they must paint over the header too); this view only
// signals intent (`onAddGear`/`onOpenItem`) and reorders its own copy of
// the chain locally (the one mutation it can make without picker/network
// input), handing the result to the owner via `onChanged`.
class StackEditView : public juce::Component {
public:
    StackEditView ();

    void setStack (const nam::Stack& stack, int idx);
    // Dismisses the REMOVE STACK confirm sheet if it's open. Returns true
    // if it was (and got closed) -- lets the owner's back-button chain
    // treat this the same way it treats the picker/item-sheet overlays
    // (close the overlay first, don't pop the screen underneath it).
    bool closeConfirm ();

    std::function<void (nam::Stack)> onChanged;          // owner persists + repushes
    std::function<void ()> onRemoveStack;                // fires after the confirm sheet
    std::function<void (nam::GearType)> onAddGear;       // "+ ADD" -> owner opens the picker
    std::function<void (juce::String uid)> onOpenItem;   // tap-to-detail -> owner opens the sheet

    void paint (juce::Graphics&) override;
    void resized () override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void layout ();
    void layoutGuided (juce::Rectangle<int> content);
    // AMP card sub-block of layoutGuided, split out to stay under the
    // ~60-line function-size rule. See definition for details.
    int layoutAmpCard (const nam::ChainItem* amp, int w, int y);
    void layoutFreeform (juce::Rectangle<int> content);
    void paintGuided (juce::Graphics&, int dy) const;
    void paintFreeform (juce::Graphics&, int dy) const;
    void paintConfirm (juce::Graphics&) const;
    void moveItem (const juce::String& uid, int delta);
    void handleContentTap (juce::Point<int> contentLocal);
    void openConfirm ();
    const nam::ChainItem* findItem (const juce::String& uid) const;

    nam::Stack stack_;
    int idx_ = -1;
    bool freeform_ = false;

    // ROUTING is two rows at this width: micro-label + FREEFORM toggle on
    // top, the three routing pills (sized to their own text, never a fixed
    // width that could clip "STEREO") below.
    juce::Rectangle<int> routingLabelRect_, freeformToggleRect_, routingPillsRowRect_;
    std::array<juce::Rectangle<int>, 3> routingPillRects_;

    juce::Rectangle<int> contentArea_;   // vertical-scroll viewport, below routingPillsRowRect_
    int contentH_ = 0;
    float scrollY_ = 0.0f, pressScrollY_ = 0.0f;

    // Guided-mode content-local rects (0,0 = contentArea_'s top-left).
    juce::Rectangle<int> pedalsHeaderRect_, pedalsAddRect_;
    struct ItemRect {
        juce::Rectangle<int> body;
        juce::String uid;
    };
    std::vector<ItemRect> pedalCardRects_;
    juce::Rectangle<int> ampHeaderRect_, ampAddRect_, ampCardRect_;
    juce::String ampUid_;
    std::vector<juce::Rectangle<int>> ampChannelPillRects_;
    juce::Rectangle<int> cabRowRect_, cabAddRect_;
    juce::String cabUid_;
    juce::Rectangle<int> postHeaderRect_, postAddRect_;
    std::vector<ItemRect> postRowRects_;

    // Freeform-mode content-local rects.
    struct FreeformRowRect {
        juce::Rectangle<int> body, upBtn, downBtn;
        juce::String uid;
    };
    std::vector<FreeformRowRect> freeformRowRects_;
    juce::Rectangle<int> freeformAddRect_;

    juce::Rectangle<int> removeStackRect_;

    // Confirm-remove sheet: small and inline (not worth its own overlay
    // TU); scoped to this view's own bounds, not the full screen.
    bool confirmOpen_ = false;
    juce::Rectangle<int> confirmRect_, confirmRemoveBtn_, confirmCancelBtn_;

    bool pressedInContent_ = false;
    juce::Point<int> pressPos_;
    bool moved_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StackEditView)
};
