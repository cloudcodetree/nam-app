#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "model/StackModel.h"

// Stack detail EDIT content: the freeform reorderable chain list (signal
// top->bottom, tap a row for the item sheet, ↑/↓ to reorder, "+ ADD GEAR",
// REMOVE STACK row with an inline confirm). Pure content -- the shared
// gear-picker/item-sheet overlays are owned by StackDetailScreen (they must
// paint over the header too); this view only signals intent
// (`onAddGear`/`onOpenItem`) and reorders its own copy of the chain locally
// (the one mutation it can make without picker/network input), handing the
// result to the owner via `onChanged`.
//
// Stripped 2026-08-15 (Chris): guided mode (PEDALS/AMP/POST sections), the
// ROUTING pill row, and the FREEFORM toggle are gone -- this is now the
// only edit mode there is. See decisions.md.
class StackEditView : public juce::Component {
public:
    StackEditView ();

    void setStack (const nam::Stack& stack, int idx);
    // Gear-thumbnail lookup, keyed by toneId (an amp's key is its ACTIVE
    // channel's toneId, not the item's own -- see thumbFor). Presentation
    // only: pushed by the owner (AppShellStackThumbs.cpp) alongside setStack.
    void setThumbs (std::map<std::string, juce::Image> thumbs);
    // Dismisses the REMOVE STACK confirm sheet if it's open. Returns true
    // if it was (and got closed) -- lets the owner's back-button chain
    // treat this the same way it treats the picker/item-sheet overlays
    // (close the overlay first, don't pop the screen underneath it).
    bool closeConfirm ();

    std::function<void (nam::Stack)> onChanged;          // owner persists + repushes
    std::function<void ()> onRemoveStack;                // fires after the confirm sheet
    std::function<void (nam::GearType)> onAddGear;       // "+ ADD GEAR" -> owner opens the picker
    std::function<void (juce::String uid)> onOpenItem;   // tap-to-detail -> owner opens the sheet

    void paint (juce::Graphics&) override;
    void resized () override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void layout ();
    void paintConfirm (juce::Graphics&) const;
    // uid is by value, not const&: a caller iterating freeformRowRects_
    // (handleContentTap) can pass a reference INTO that vector as `uid`
    // (fr.uid) -- layout(), called below before this returns, clears
    // freeformRowRects_ and would leave a const& dangling. Taking it by
    // value copies the (refcounted, so cheap) juce::String at the call
    // site, before layout() can invalidate anything it might alias.
    void moveItem (juce::String uid, int delta);
    void handleContentTap (juce::Point<int> contentLocal);
    void openConfirm ();
    const nam::ChainItem* findItem (const juce::String& uid) const;
    // Resolves the thumbnail for `it` -- its active channel for an amp
    // (mirrors AppShellStackThumbs.cpp's pushStackThumbs key), its own
    // toneId otherwise. {} (paints the placeholder) if not in thumbs_.
    juce::Image thumbFor (const nam::ChainItem& it) const;

    nam::Stack stack_;
    int idx_ = -1;
    std::map<std::string, juce::Image> thumbs_;

    juce::Rectangle<int> contentArea_;   // vertical-scroll viewport, this view's full bounds
    int contentH_ = 0;
    float scrollY_ = 0.0f, pressScrollY_ = 0.0f;

    // Content-local rects (0,0 = contentArea_'s top-left).
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
