#include "app/ui/StackEditView.h"
#include "app/ui/NamLookAndFeel.h"
#include "app/ui/StackWidgets.h"

// State, layout, and hit-testing. Painting lives in StackEditViewPaint.cpp
// -- this component alone would exceed the 400-line file cap (no-god-files
// rule), so it splits the same way AppShell.cpp/AppShellChrome.cpp/
// AppShellStacks.cpp already do: one class, several TUs.
using namespace nam::ui;

StackEditView::StackEditView () { setInterceptsMouseClicks (true, true); }

void StackEditView::setStack (const nam::Stack& stack, int idx) {
    stack_ = stack;
    idx_ = idx;
    // A stack switch (or even a same-stack repush) must never carry a
    // REMOVE STACK confirm forward -- it was scoped to whatever stack was
    // showing when it opened, and leaving it open would let a reflexive
    // confirm tap delete a completely different stack the user never
    // asked to remove (found in review: BACK out of an open confirm, open
    // another stack, "Remove '{other name}'?" is already up).
    confirmOpen_ = false;
    layout ();
    repaint ();
}

bool StackEditView::closeConfirm () {
    if (!confirmOpen_) return false;
    confirmOpen_ = false;
    repaint ();
    return true;
}

void StackEditView::setThumbs (std::map<std::string, juce::Image> thumbs) {
    thumbs_ = std::move (thumbs);
    repaint ();
}

juce::Image StackEditView::thumbFor (const nam::ChainItem& it) const {
    std::string id = it.toneId;
    if (it.type == nam::GearType::Amp && !it.channels.empty () && it.activeChannel >= 0 &&
        it.activeChannel < (int)it.channels.size ())
        id = it.channels[(size_t)it.activeChannel].toneId;
    if (id.empty ()) return {};
    auto found = thumbs_.find (id);
    return found != thumbs_.end () ? found->second : juce::Image ();
}

const nam::ChainItem* StackEditView::findItem (const juce::String& uid) const {
    for (const auto& it : stack_.chain)
        if (juce::String (it.uid) == uid) return &it;
    return nullptr;
}

void StackEditView::layout () {
    auto b = getLocalBounds ();
    if (b.isEmpty ()) return;

    contentArea_ = b;

    freeformRowRects_.clear ();
    const int w = contentArea_.getWidth ();
    int y = 32;   // hint band, no hit-test
    constexpr int rowH = 56, gap = 8, btnW = 30;
    for (const auto& it : stack_.chain) {
        FreeformRowRect fr;
        fr.body = { 0, y, w, rowH };
        fr.uid = juce::String (it.uid);
        fr.downBtn = { w - btnW, y, btnW, rowH };
        fr.upBtn = { w - btnW * 2 - 4, y, btnW, rowH };
        freeformRowRects_.push_back (fr);
        y += rowH + gap;
    }
    freeformAddRect_ = { 0, y, w, 48 };
    y += 48 + 22;
    removeStackRect_ = { 0, y, w, 48 };
    y += 48 + 20;

    contentH_ = y;
    scrollY_ =
        juce::jlimit (0.0f, (float)juce::jmax (0, contentH_ - contentArea_.getHeight ()), scrollY_);
}

void StackEditView::resized () { layout (); }

void StackEditView::openConfirm () {
    confirmOpen_ = true;
    const auto b = getLocalBounds ();
    const int w = juce::jmin (300, b.getWidth () - 48);
    confirmRect_ = juce::Rectangle<int> (w, 150).withCentre (b.getCentre ());
    auto inner = confirmRect_.reduced (18, 16);
    auto btnRow = inner.removeFromBottom (40);
    const int half = (btnRow.getWidth () - 12) / 2;
    confirmCancelBtn_ = btnRow.removeFromLeft (half);
    btnRow.removeFromLeft (12);
    confirmRemoveBtn_ = btnRow;
    repaint ();
}

void StackEditView::moveItem (const juce::String& uid, int delta) {
    int idx = -1;
    for (size_t i = 0; i < stack_.chain.size (); ++i)
        if (juce::String (stack_.chain[i].uid) == uid) {
            idx = (int)i;
            break;
        }
    if (idx < 0) return;
    const int newIdx = juce::jlimit (0, (int)stack_.chain.size () - 1, idx + delta);
    if (newIdx == idx) return;
    std::swap (stack_.chain[(size_t)idx], stack_.chain[(size_t)newIdx]);
    layout ();
    repaint ();
    if (onChanged) onChanged (stack_);
}

void StackEditView::handleContentTap (juce::Point<int> cp) {
    for (const auto& fr : freeformRowRects_) {
        if (fr.upBtn.contains (cp)) {
            moveItem (fr.uid, -1);
            return;
        }
        if (fr.downBtn.contains (cp)) {
            moveItem (fr.uid, 1);
            return;
        }
        if (fr.body.contains (cp)) {
            if (onOpenItem) onOpenItem (fr.uid);
            return;
        }
    }
    if (freeformAddRect_.contains (cp)) {
        if (onAddGear) onAddGear (nam::GearType::Pedal);
        return;
    }
    if (removeStackRect_.contains (cp)) openConfirm ();
}

void StackEditView::mouseDown (const juce::MouseEvent& e) {
    const auto p = e.getPosition ();
    pressPos_ = p;
    moved_ = false;
    pressedInContent_ = false;

    if (confirmOpen_) return;   // resolved entirely on mouseUp (simple tap dialog)

    if (contentArea_.contains (p)) {
        pressedInContent_ = true;
        pressScrollY_ = scrollY_;
    }
}

void StackEditView::mouseDrag (const juce::MouseEvent& e) {
    if (!pressedInContent_) return;
    const auto p = e.getPosition ();
    const int dy = p.y - pressPos_.y;
    if (std::abs (dy) > 8) moved_ = true;
    if (moved_) {
        scrollY_ = juce::jlimit (0.0f, (float)juce::jmax (0, contentH_ - contentArea_.getHeight ()),
                                 pressScrollY_ - (float)dy);
        repaint (contentArea_);
    }
}

void StackEditView::mouseUp (const juce::MouseEvent& e) {
    const auto p = e.getPosition ();
    const bool tap = !moved_;
    pressedInContent_ = false;

    if (confirmOpen_) {
        if (tap) {
            if (confirmRemoveBtn_.contains (p)) {
                confirmOpen_ = false;
                if (onRemoveStack) onRemoveStack ();
            } else if (confirmCancelBtn_.contains (p) || !confirmRect_.contains (p)) {
                confirmOpen_ = false;
            }
            repaint ();
        }
        return;
    }
    if (!tap) return;

    if (!contentArea_.contains (p)) return;
    handleContentTap ({ p.x - contentArea_.getX (), p.y - contentArea_.getY () + (int)scrollY_ });
}
