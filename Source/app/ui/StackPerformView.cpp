#include "app/ui/StackPerformView.h"
#include <cmath>
#include "app/ui/NamLookAndFeel.h"
#include "app/ui/StackWidgets.h"

// State, layout, and hit-testing. Painting lives in StackPerformViewPaint.cpp
// -- this component alone would exceed the 400-line file cap (no-god-files
// rule), same split StackEditView/StackEditViewPaint already use.
using namespace nam::ui;

namespace {
const juce::String kUnassignedToast =
    juce::String::fromUTF8 ("Assign in EDIT \xE2\x86\x92 tap gear \xE2\x86\x92 FOOTSWITCH");
}   // namespace

StackPerformView::StackPerformView () { setInterceptsMouseClicks (true, true); }

void StackPerformView::setStack (const nam::Stack& stack, int pos, int count) {
    stack_ = stack;
    pos_ = pos;
    count_ = juce::jmax (1, count);
    layoutGrid ();
    repaint ();
}

const nam::ChainItem* StackPerformView::findChainItem (const juce::String& uid) const {
    for (const auto& it : stack_.chain)
        if (juce::String (it.uid) == uid) return &it;
    return nullptr;
}

void StackPerformView::layout () {
    auto b = getLocalBounds ();
    if (b.isEmpty ()) return;

    auto header = b.removeFromTop (64);
    // Exit chevron: dedicated ≥44px hit rect, distinct from the setlist
    // prev/next arrows that flank the centre label (task-4 brief: "The exit
    // ‹ chevron hit-rect must be ≥44px").
    exitRect_ = header.removeFromLeft (56).withSizeKeepingCentre (44, 44);
    prevRect_ = header.removeFromLeft (40).withSizeKeepingCentre (28, 44);
    nextRect_ = header.removeFromRight (40).withSizeKeepingCentre (28, 44);
    headerLabelRect_ = header;

    auto toggleRow = b.removeFromTop (44).reduced (20, 6);
    scenesToggleRect_ = toggleRow.removeFromLeft (toggleRow.getWidth () / 2).reduced (2, 0);
    stompToggleRect_ = toggleRow.reduced (2, 0);

    contentArea_ = b.reduced (16, 8);
    layoutGrid ();
}

void StackPerformView::layoutGrid () {
    cells_.clear ();
    if (!stompMode_) {
        for (int i = 0; i < (int)stack_.scenes.size (); ++i)
            cells_.push_back ({ {}, CellKind::Scene, i, {} });
        cells_.push_back ({ {}, CellKind::Amp, 0, {} });
        cells_.push_back ({ {}, CellKind::Tap, 0, {} });
        cells_.push_back ({ {}, CellKind::Tuner, 0, {} });
        cells_.push_back ({ {}, CellKind::Next, 0, {} });
    } else {
        // STOMP shows a switch for every FS1-8 slot -- not one switch per
        // chain item that happens to have an fs -- because an unassigned
        // slot still needs something to tap for the "Assign in EDIT..."
        // toast (handleCellTap). Filtering by fs != 0 would leave no
        // switch to press for a slot nothing is assigned to. See
        // docs/wiki/decisions.md (2026-08-15, STOMP fixed-slot resolution).
        for (int fs = 1; fs <= 8; ++fs) {
            juce::String uid;
            for (const auto& it : stack_.chain)
                if (it.fs == fs) {
                    uid = juce::String (it.uid);
                    break;
                }
            cells_.push_back ({ {}, CellKind::Stomp, fs, uid });
        }
    }

    if (contentArea_.isEmpty ()) return;
    constexpr int gap = 10, minCellH = 72, maxCellH = 148, cols = 4;
    const int cw = juce::jmax (40, (contentArea_.getWidth () - gap * (cols - 1)) / cols);
    const int rows = (int)((cells_.size () + (size_t)cols - 1) / (size_t)cols);
    // Stage use wants BIG touch targets, not a fixed 72px grid stranded at
    // the top of the screen with the rest dead space -- a light setlist
    // (few scenes, or STOMP's fixed 2-row grid) grows cells to fill the
    // available height instead, capped so a single row doesn't get absurd.
    const int cellH = rows > 0
                          ? juce::jlimit (minCellH, maxCellH,
                                          (contentArea_.getHeight () - gap * (rows - 1)) / rows)
                          : minCellH;
    contentH_ = rows > 0 ? rows * (cellH + gap) - gap : 0;
    // When the grid is shorter than the viewport (no scroll needed), centre
    // the block vertically instead of pinning it to the top -- otherwise a
    // light setlist leaves the grown cells huddled up top with the same
    // dead space just moved below them.
    const int extraY = juce::jmax (0, (contentArea_.getHeight () - contentH_) / 2);
    int col = 0, row = 0;
    for (auto& cell : cells_) {
        cell.body = { col * (cw + gap), extraY + row * (cellH + gap), cw, cellH };
        if (++col >= cols) {
            col = 0;
            ++row;
        }
    }
    scrollY_ =
        juce::jlimit (0.0f, (float)juce::jmax (0, contentH_ - contentArea_.getHeight ()), scrollY_);
}

void StackPerformView::resized () { layout (); }

void StackPerformView::recordTap () {
    const double now = juce::Time::getMillisecondCounterHiRes ();
    if (!tapTimesMs_.empty () && now - tapTimesMs_.back () > 2000.0) tapTimesMs_.clear ();
    tapTimesMs_.push_back (now);
    if (tapTimesMs_.size () > 5) tapTimesMs_.erase (tapTimesMs_.begin ());
    if (tapTimesMs_.size () >= 2) {
        double sum = 0.0;
        int n = 0;
        for (size_t i = 1; i < tapTimesMs_.size (); ++i) {
            sum += tapTimesMs_[i] - tapTimesMs_[i - 1];
            ++n;
        }
        const double avgMs = sum / (double)n;
        if (avgMs > 1.0) bpm_ = 60000.0 / avgMs;
    }
}

void StackPerformView::handleCellTap (const Cell& cell) {
    switch (cell.kind) {
        case CellKind::Scene:
            if (onSceneTap) onSceneTap (cell.index);
            break;
        case CellKind::Amp:
            if (onAmpCycle) onAmpCycle ();
            break;
        case CellKind::Tap: recordTap (); break;
        case CellKind::Tuner:
            if (onTuner) onTuner ();
            break;
        case CellKind::Next:
            if (onNextStack) onNextStack ();
            break;
        case CellKind::Stomp:
            if (cell.uid.isNotEmpty ()) {
                if (onStompTap) onStompTap (cell.uid);
            } else {
                showToast (*this, kUnassignedToast);
            }
            break;
    }
    repaint (contentArea_);
}

void StackPerformView::mouseDown (const juce::MouseEvent& e) {
    pressPos_ = e.getPosition ();
    moved_ = false;
    pressedInContent_ = contentArea_.contains (pressPos_);
    if (pressedInContent_) pressScrollY_ = scrollY_;
}

void StackPerformView::mouseDrag (const juce::MouseEvent& e) {
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

void StackPerformView::mouseUp (const juce::MouseEvent& e) {
    const auto p = e.getPosition ();
    const bool tap = !moved_;
    pressedInContent_ = false;
    if (!tap) return;

    if (exitRect_.contains (p)) {
        if (onExit) onExit ();
        return;
    }
    if (prevRect_.expanded (4).contains (p)) {
        if (onPrevStack) onPrevStack ();
        return;
    }
    if (nextRect_.expanded (4).contains (p)) {
        if (onNextStack) onNextStack ();
        return;
    }
    if (scenesToggleRect_.contains (p) && stompMode_) {
        stompMode_ = false;
        layoutGrid ();
        repaint ();
        return;
    }
    if (stompToggleRect_.contains (p) && !stompMode_) {
        stompMode_ = true;
        layoutGrid ();
        repaint ();
        return;
    }
    if (!contentArea_.contains (p)) return;

    const juce::Point<int> cp (p.x - contentArea_.getX (),
                               p.y - contentArea_.getY () + (int)scrollY_);
    for (const auto& cell : cells_)
        if (cell.body.contains (cp)) {
            handleCellTap (cell);
            return;
        }
}
