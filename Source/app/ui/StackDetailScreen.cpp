#include "app/ui/StackDetailScreen.h"
#include "app/ui/NamLookAndFeel.h"
#include "app/ui/StackWidgets.h"

using namespace nam::ui;

namespace {
const juce::String kBackGlyph = juce::String::fromUTF8 ("\xE2\x80\xB9");   // ‹
const juce::String kEllipsis = juce::String::fromUTF8 ("\xE2\x80\xA6");    // …
}   // namespace

StackDetailScreen::StackDetailScreen () { setOpaque (true); }

void StackDetailScreen::setStack (const nam::Stack& stack, int idx) {
    stack_ = stack;
    idx_ = idx;
    repaint ();
}

void StackDetailScreen::selectTab (bool perform) {
    performTab_ = perform;
    if (onTabChanged) onTabChanged (performTab_);
    repaint ();
}

void StackDetailScreen::layout () {
    auto b = getLocalBounds ();
    headerRect_ = b.removeFromTop (52);
    gearRect_ = { headerRect_.getRight () - 52, headerRect_.getCentreY () - 21, 42, 42 };

    auto tabRow = b.removeFromTop (52).reduced (20, 8);
    backRect_ = tabRow.removeFromLeft (28);
    auto pill = tabRow.removeFromRight (150).withSizeKeepingCentre (150, 32);
    editTabRect_ = pill.removeFromLeft (pill.getWidth () / 2);
    performTabRect_ = pill;
    nameRect_ = tabRow;

    bodyRect_ = b.reduced (20, 12);
}

void StackDetailScreen::resized () { layout (); }

void StackDetailScreen::paint (juce::Graphics& g) {
    paintHeroBackground (g, getLocalBounds ());
    drawStacksBrandHeader (g, headerRect_, gearRect_);

    g.setFont (uiFont (20.0f, false));
    g.setColour (col::inkA (0.7f));
    g.drawText (kBackGlyph, backRect_, juce::Justification::centredLeft, false);

    g.setFont (displayFont (20.0f));
    g.setColour (col::ink);
    g.drawText (juce::String (stack_.name), nameRect_, juce::Justification::centredLeft, true);

    auto tabPill = [&] (juce::Rectangle<int> r, const juce::String& label, bool active) {
        drawPill (g, r.toFloat (), active ? col::accent : juce::Colours::transparentBlack,
                  active ? col::accent : col::inkA (0.2f));
        g.setFont (uiFontTracked (10.0f, true));
        g.setColour (active ? col::inkOnAccent : col::inkA (0.6f));
        g.drawText (label, r, juce::Justification::centred, false);
    };
    tabPill (editTabRect_, "EDIT", !performTab_);
    tabPill (performTabRect_, "PERFORM", performTab_);

    g.setFont (uiFont (13.0f, false));
    g.setColour (col::inkA (0.35f));
    g.drawText (performTab_ ? "PERFORM " + kEllipsis + " coming soon"
                            : "EDIT " + kEllipsis + " coming next task",
                bodyRect_, juce::Justification::centred, false);
}

void StackDetailScreen::mouseDown (const juce::MouseEvent& e) {
    const auto p = e.getPosition ();

    if (gearRect_.expanded (4).contains (p)) {
        if (onSettings) onSettings ();
        return;
    }
    if (backRect_.expanded (8).contains (p)) {
        if (onBack) onBack ();
        return;
    }
    if (editTabRect_.contains (p) && performTab_) {
        selectTab (false);
        return;
    }
    if (performTabRect_.contains (p) && !performTab_) {
        selectTab (true);
        return;
    }
}
