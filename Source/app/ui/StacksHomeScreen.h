#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "app/ui/StackCreateWizard.h"
#include "model/StackModel.h"

// Stacks Home: the ordered-chain rig list (replaces the old fixed-6-slot
// StacksScreen accordion). Rows navigate to a dedicated Stack detail screen
// instead of expanding in place, and "+ NEW STACK" is the first card in the
// same list rather than a header pill. Presentation only -- data and actions
// are injected by the owner (AppShellStacks.cpp).
//
// Stripped 2026-08-15 (Chris): the brand wordmark, screen title, blurb,
// settings gear, and SETLIST chip strip are all gone. The gear was redundant with
// the bottom nav's status orb, and the chips only set a "current" marker
// whose one real consumer (PERFORM's setlist stepping) prefers the open
// stack's own index anyway.
class StacksHomeScreen : public juce::Component {
public:
    StacksHomeScreen ();

    void setStacks (std::vector<nam::Stack> stacks, int current);
    // Gear-thumbnail lookup, keyed by toneId (the active amp channel, or
    // failing that the cab) -- pushed alongside setStacks by the owner
    // (AppShellStackThumbs.cpp); presentation only, never fetched here.
    void setThumbs (std::map<std::string, juce::Image> thumbs);

    std::function<void ()> onCreate;       // "+ NEW STACK" card
    std::function<void (int)> onOpen;      // row body tap -> Detail EDIT
    std::function<void (int)> onPerform;   // "PERFORM" pill -> Detail PERFORM

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
    juce::String metaLine (const nam::Stack&) const;
    // One rig row's card, pulled out of paint() to stay under the ~60-line
    // house rule (clang-tidy readability-function-size) once the thumbnail
    // reflow landed. `dy` is the same list-scroll offset paint() already
    // computes; `i` indexes both stacks_ and rowRects_.
    void paintRigCard (juce::Graphics&, size_t i, int dy) const;

    std::vector<nam::Stack> stacks_;
    int current_ = -1;
    std::map<std::string, juce::Image> thumbs_;
    const juce::Image* thumbFor (const nam::Stack&) const;   // active amp, else cab, else nullptr

    StackCreateWizard wizard_;

    juce::Rectangle<int> listArea_;

    // List content is content-local (y=0 at the list's top), translated by
    // scrollY_ at paint/hit time. The NEW STACK card is the first card and
    // shares the rows' geometry.
    juce::Rectangle<int> newCardRect_;
    struct RowRect {
        juce::Rectangle<int> body, performBtn;
    };
    std::vector<RowRect> rowRects_;
    int listContentH_ = 0;
    float scrollY_ = 0.0f, pressScrollY_ = 0.0f;

    // Press/drag/tap state machine for the one scrollable region.
    bool pressedInList_ = false;
    juce::Point<int> pressPos_;
    bool moved_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StacksHomeScreen)
};
