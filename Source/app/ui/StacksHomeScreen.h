#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "model/StackModel.h"

// Stacks Home: the ordered-chain rig list (replaces the old fixed-6-slot
// StacksScreen accordion). Rows navigate to a dedicated Stack detail screen,
// whose freeform editor is now the ONLY way to work on a rig, and "+ NEW
// STACK" is the first card in the same list rather than a header pill.
// Presentation only -- data and actions are injected by the owner
// (AppShellStacks.cpp).
//
// Stripped 2026-08-15 (Chris): the brand wordmark, screen title, blurb,
// settings gear, and SETLIST chip strip are all gone (the gear was
// redundant with the bottom nav's status orb); the CONTROLS pill/PERFORM
// entry point, the create wizard, and the routing badge followed in the
// same pass -- Stacks is now just a list of rigs, each an ordered chain you
// edit. See decisions.md.
class StacksHomeScreen : public juce::Component {
public:
    StacksHomeScreen ();

    void setStacks (std::vector<nam::Stack> stacks);
    // Gear-thumbnail lookup, keyed by toneId (the active amp channel, or
    // failing that the cab) -- pushed alongside setStacks by the owner
    // (AppShellStackThumbs.cpp); presentation only, never fetched here.
    void setThumbs (std::map<std::string, juce::Image> thumbs);

    std::function<void ()> onCreate;    // "+ NEW STACK" card
    std::function<void (int)> onOpen;   // row body tap -> Detail EDIT

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
    std::map<std::string, juce::Image> thumbs_;
    const juce::Image* thumbFor (const nam::Stack&) const;   // active amp, else cab, else nullptr

    juce::Rectangle<int> listArea_;

    // List content is content-local (y=0 at the list's top), translated by
    // scrollY_ at paint/hit time. The NEW STACK card is the first card and
    // shares the rows' geometry.
    juce::Rectangle<int> newCardRect_;
    struct RowRect {
        juce::Rectangle<int> body;
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
