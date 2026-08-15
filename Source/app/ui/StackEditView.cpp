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

const nam::ChainItem* StackEditView::findItem (const juce::String& uid) const {
    for (const auto& it : stack_.chain)
        if (juce::String (it.uid) == uid) return &it;
    return nullptr;
}

void StackEditView::layout () {
    auto b = getLocalBounds ();
    if (b.isEmpty ()) return;

    // Row 1: "ROUTING" micro-label + FREEFORM toggle. Row 2: the three
    // routing pills, each sized to its own label text (never a fixed
    // shared width -- at this component's width a 3-way equal split left
    // no room for "STEREO", which clipped to "STERE").
    auto labelRow = b.removeFromTop (20);
    routingLabelRect_ = labelRow.withWidth (62);
    freeformToggleRect_ = labelRow.removeFromRight (100);

    routingPillsRowRect_ = b.removeFromTop (32).reduced (0, 2);
    {
        auto row = routingPillsRowRect_;
        const char* labels[3] = { "SINGLE", "A/B", "STEREO" };
        const auto font = uiFontTracked (8.0f, true);
        int x = row.getX ();
        for (int i = 0; i < 3; ++i) {
            const int pw = (int)juce::GlyphArrangement::getStringWidth (font, labels[i]) + 22;
            routingPillRects_[(size_t)i] = { x, row.getY (), pw, row.getHeight () };
            x += pw + 8;
        }
    }
    b.removeFromTop (4);

    contentArea_ = b;
    if (freeform_) layoutFreeform (contentArea_);
    else layoutGuided (contentArea_);
}

void StackEditView::layoutGuided (juce::Rectangle<int> content) {
    const int w = content.getWidth ();
    constexpr int headerH = 22, hintH = 16, ruleGap = 16, sectionGap = 22;
    int y = 0;

    pedalsHeaderRect_ = { 0, y, w - 76, headerH };
    pedalsAddRect_ = { w - 72, y, 72, headerH };
    y += headerH + hintH + ruleGap;
    pedalCardRects_.clear ();
    {
        constexpr int cw = 88, ch = 112, gap = 10;
        int x = 0, rowY = y;
        bool any = false;
        for (const auto& it : stack_.chain) {
            if (it.type != nam::GearType::Pedal) continue;
            any = true;
            if (x + cw > w && x > 0) {
                x = 0;
                rowY += ch + gap;
            }
            pedalCardRects_.push_back ({ { x, rowY, cw, ch }, juce::String (it.uid) });
            x += cw + gap;
        }
        y = any ? rowY + ch : y + 40;
    }
    y += sectionGap;

    ampHeaderRect_ = { 0, y, w - 76, headerH };
    const auto* amp = nam::StackModel::activeAmp (stack_);
    ampAddRect_ =
        amp == nullptr ? juce::Rectangle<int> (w - 72, y, 72, headerH) : juce::Rectangle<int> ();
    y += headerH + ruleGap;
    ampChannelPillRects_.clear ();
    if (amp != nullptr) {
        ampUid_ = juce::String (amp->uid);
        constexpr int pillH = 28, pillGap = 8;
        // Matches paintGuided's card-content cursor exactly: reduced(14,10)
        // top pad (10) + name/FS row (26) + gap (6) + grille (24) + gap (8)
        // + "CH" label (12) = 86px before the pill row starts.
        constexpr int topContentH = 86;
        // Pill widths capped to the card's own width (a single very long
        // channel name used to be able to overflow it horizontally on its
        // own -- paintGuided elides the drawn text into whatever width it
        // gets, but the pill rect itself still needs a ceiling). Row count
        // computed up front so the card can size itself to fit every
        // channel row instead of a fixed 128px that clipped the CH pills
        // against the card's own bottom edge whenever channels wrapped.
        std::vector<int> pillW;
        pillW.reserve (amp->channels.size ());
        int rows = 1, rowX = 0;
        for (const auto& ch : amp->channels) {
            const int pw = juce::jmin (
                w - 8, juce::jmax (56, (int)juce::GlyphArrangement::getStringWidth (
                                           uiFont (10.0f, true), juce::String (ch.title)) +
                                           24));
            pillW.push_back (pw);
            if (rowX + pw > w && rowX > 0) {
                ++rows;
                rowX = 0;
            }
            rowX += pw + pillGap;
        }
        const int cardH = topContentH + rows * pillH + (rows - 1) * pillGap + 10;
        ampCardRect_ = { 0, y, w, cardH };
        int x = 0, rowY = y + topContentH;
        for (size_t i = 0; i < amp->channels.size (); ++i) {
            const int pw = pillW[i];
            if (x + pw > w && x > 0) {
                x = 0;
                rowY += pillH + pillGap;
            }
            ampChannelPillRects_.push_back ({ x, rowY, pw, pillH });
            x += pw + pillGap;
        }
        y += cardH;
    } else {
        ampUid_ = {};
        ampCardRect_ = { 0, y, w, 40 };
        y += 40;
    }
    y += 12;

    const auto* cab = nam::StackModel::cabOf (stack_);
    cabUid_ = cab != nullptr ? juce::String (cab->uid) : juce::String ();
    cabRowRect_ = { 0, y, w, 52 };
    cabAddRect_ = cab == nullptr ? cabRowRect_ : juce::Rectangle<int> ();
    y += 52 + sectionGap;

    postHeaderRect_ = { 0, y, w - 76, headerH };
    postAddRect_ = { w - 72, y, 72, headerH };
    y += headerH + hintH + ruleGap;
    postRowRects_.clear ();
    {
        constexpr int rh = 56, gap = 8;
        for (const auto& it : stack_.chain) {
            if (it.type != nam::GearType::Post) continue;
            postRowRects_.push_back ({ { 0, y, w, rh }, juce::String (it.uid) });
            y += rh + gap;
        }
        if (postRowRects_.empty ()) y += 40;
    }
    y += sectionGap;

    removeStackRect_ = { 0, y, w, 48 };
    y += 48 + 20;

    contentH_ = y;
    scrollY_ =
        juce::jlimit (0.0f, (float)juce::jmax (0, contentH_ - contentArea_.getHeight ()), scrollY_);
}

void StackEditView::layoutFreeform (juce::Rectangle<int> content) {
    const int w = content.getWidth ();
    int y = 32;   // hint band, no hit-test

    freeformRowRects_.clear ();
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
    if (freeform_) {
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
        return;
    }

    if (!pedalsAddRect_.isEmpty () && pedalsAddRect_.contains (cp)) {
        if (onAddGear) onAddGear (nam::GearType::Pedal);
        return;
    }
    for (const auto& pc : pedalCardRects_)
        if (pc.body.contains (cp)) {
            if (onOpenItem) onOpenItem (pc.uid);
            return;
        }
    if (!ampAddRect_.isEmpty () && ampAddRect_.contains (cp)) {
        if (onAddGear) onAddGear (nam::GearType::Amp);
        return;
    }
    if (!ampUid_.isEmpty () && ampCardRect_.contains (cp)) {
        if (onOpenItem) onOpenItem (ampUid_);
        return;
    }
    if (!cabAddRect_.isEmpty () && cabAddRect_.contains (cp)) {
        if (onAddGear) onAddGear (nam::GearType::Cab);
        return;
    }
    if (!cabUid_.isEmpty () && cabRowRect_.contains (cp)) {
        if (onOpenItem) onOpenItem (cabUid_);
        return;
    }
    if (!postAddRect_.isEmpty () && postAddRect_.contains (cp)) {
        if (onAddGear) onAddGear (nam::GearType::Post);
        return;
    }
    for (const auto& pr : postRowRects_)
        if (pr.body.contains (cp)) {
            if (onOpenItem) onOpenItem (pr.uid);
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
    if (freeformToggleRect_.expanded (4).contains (p)) return;
    for (int i = 0; i < 3; ++i)
        if (routingPillRects_[(size_t)i].expanded (2).contains (p)) return;

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

    if (freeformToggleRect_.expanded (4).contains (p)) {
        freeform_ = !freeform_;
        layout ();
        repaint ();
        return;
    }
    for (int i = 0; i < 3; ++i)
        if (routingPillRects_[(size_t)i].expanded (2).contains (p)) {
            if (i != 0) showToast (*this, "A/B & stereo routing coming soon");
            return;
        }
    if (!contentArea_.contains (p)) return;
    handleContentTap ({ p.x - contentArea_.getX (), p.y - contentArea_.getY () + (int)scrollY_ });
}
