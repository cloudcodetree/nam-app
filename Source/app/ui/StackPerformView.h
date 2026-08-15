#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
#include "model/StackModel.h"

// Stack detail PERFORM tab: full-bleed stage view. The owner
// (StackDetailScreen) gives this component its WHOLE area -- no brand
// header / back-chevron / EDIT|PERFORM tab pill survives underneath; the
// small ‹ exit chevron drawn here is the only way out (AppShell hides the
// bottom nav for the same reason, see AppShell::setNavHidden). On-screen
// switches only -- MIDI learn, the EXP row, and the physical-controller map
// from the design are a separate future plan (Phase A scope; see the SDD
// notes doc's "Stack detail — PERFORM tab" section).
//
// This view owns no audio/network/persistence state: every tap is reported
// via a callback and the owner (AppShellStacks.cpp) decides what to load
// into the engine, mutates STORED state (bypass/activeScene/activeChannel),
// and persists. It renders whatever `stack_` setStack() last pushed and
// reports intent -- except the unassigned-footswitch toast and the TAP
// switch's BPM readout, which are pure local UI state with no owner
// involvement (see StackPerformViewPaint.cpp / handleCellTap).
class StackPerformView : public juce::Component {
public:
    StackPerformView ();

    // `pos` is 1-based ("SETLIST · {pos}/{count}"); `count` is the setlist
    // length (total stacks).
    void setStack (const nam::Stack& stack, int pos, int count);

    std::function<void (int sceneIdx)> onSceneTap;
    // Fires only for an ASSIGNED footswitch slot; an unassigned tap toasts
    // locally instead (no owner mutation to make).
    std::function<void (juce::String uid)> onStompTap;
    std::function<void ()> onAmpCycle;
    std::function<void ()> onTuner;
    std::function<void ()> onNextStack;
    std::function<void ()> onPrevStack;
    std::function<void ()> onExit;

    void paint (juce::Graphics&) override;
    void resized () override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void layout ();
    void layoutGrid ();   // (re)computes cells_ for the current mode + stack
    void recordTap ();    // TAP switch: rolling BPM readout, last <=4 intervals

    enum class CellKind { Scene, Amp, Tap, Tuner, Next, Stomp };
    struct Cell {
        juce::Rectangle<int> body;   // content-local (0,0 = contentArea_'s top-left)
        CellKind kind = CellKind::Scene;
        int index = 0;      // scene index, or FS number (1..8) for Stomp
        juce::String uid;   // Stomp only: assigned chain item; "" = unassigned
    };
    void handleCellTap (const Cell&);
    const nam::ChainItem* findChainItem (const juce::String& uid) const;

    // Painting lives in StackPerformViewPaint.cpp (no-god-files split, same
    // shape as StackEditView/StackEditViewPaint).
    void paintHeader (juce::Graphics&) const;
    void paintModeToggle (juce::Graphics&) const;
    void paintGrid (juce::Graphics&, int dy) const;
    void paintCell (juce::Graphics&, const Cell&, juce::Rectangle<int> r) const;

    nam::Stack stack_;
    int pos_ = 1, count_ = 1;
    bool stompMode_ = false;   // false = SCENES, true = STOMP

    juce::Rectangle<int> exitRect_, prevRect_, nextRect_, headerLabelRect_;
    juce::Rectangle<int> scenesToggleRect_, stompToggleRect_;

    juce::Rectangle<int> contentArea_;   // vertical-scroll viewport, below the toggle row
    int contentH_ = 0;
    float scrollY_ = 0.0f, pressScrollY_ = 0.0f;
    std::vector<Cell> cells_;

    // Tap tempo: average of the last <=4 intervals between taps. A gap over
    // 2s starts a fresh sequence so a stale first tap of a new song can't
    // skew the reading.
    std::vector<double> tapTimesMs_;
    double bpm_ = 0.0;

    bool pressedInContent_ = false;
    juce::Point<int> pressPos_;
    bool moved_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StackPerformView)
};
