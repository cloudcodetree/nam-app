#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
#include "app/ui/StackCreateWizard.h"
#include "model/StackModel.h"

// Stacks Home: the ordered-chain rig list (replaces the old fixed-6-slot
// StacksScreen accordion). Rows navigate to a dedicated Stack detail screen
// instead of expanding in place; a SETLIST chip strip picks which stack is
// "current" without navigating. Presentation only -- data and actions are
// injected by the owner (AppShellStacks.cpp).
class StacksHomeScreen : public juce::Component {
public:
    StacksHomeScreen ();

    void setStacks (std::vector<nam::Stack> stacks, int current);

    std::function<void ()> onCreate;          // "+ NEW STACK"
    std::function<void (int)> onOpen;         // row body tap -> Detail EDIT
    std::function<void (int)> onPerform;      // "PERFORM" pill -> Detail PERFORM
    std::function<void (int)> onSetCurrent;   // SETLIST chip tap
    std::function<void ()> onSettings;        // gear icon -> orb I/O flyout

    // "+ NEW STACK" opens this directly (the owner's onCreate gate wraps the
    // call, then reaches in via this accessor) -- same pattern as
    // StackDetailScreen's picker()/itemSheet(). Hosted as a screen-level
    // child here (not a height-capped overlay), so it fills Home's own
    // bounds while open; JUCE's normal child hit-testing means Home's own
    // mouseDown/paint need no changes for it at all.
    StackCreateWizard& wizard () { return wizard_; }
    // Closes the wizard if it's open (Android back button, wired in
    // AppShell::handleBackButton before the Detail checks). Returns whether
    // it was open -- nothing persists from an unsaved wizard, so there is no
    // confirm step, unlike Detail's overlay dismissal chain.
    bool closeWizard ();

    void paint (juce::Graphics&) override;
    void resized () override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void layout ();
    juce::String chipLabel (size_t i) const;
    juce::String metaLine (const nam::Stack&) const;

    std::vector<nam::Stack> stacks_;
    int current_ = -1;

    StackCreateWizard wizard_;

    juce::Rectangle<int> headerRect_, gearRect_, titleRowRect_, newBtnRect_, subtitleRect_,
        setlistLabelRect_, chipsRect_, listArea_;

    // SETLIST strip: horizontal-scroll chips, content-local rects (x=0 at
    // the strip's left edge) translated by chipScrollX_ at paint/hit time.
    std::vector<juce::Rectangle<int>> chipRects_;
    int chipsContentW_ = 0;
    float chipScrollX_ = 0.0f, chipPressScrollX_ = 0.0f;

    // Stack rows: vertical-scroll list, same content-local convention.
    struct RowRect {
        juce::Rectangle<int> body, performBtn;
    };
    std::vector<RowRect> rowRects_;
    int listContentH_ = 0;
    float scrollY_ = 0.0f, pressScrollY_ = 0.0f;

    // One press/drag/tap state machine shared by both scrollable regions;
    // pressRegion_ selects which one a drag/tap resolves against.
    enum class Region { None, Chips, List };
    Region pressRegion_ = Region::None;
    juce::Point<int> pressPos_;
    bool moved_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StacksHomeScreen)
};
