#include "app/ui/StacksHomeScreen.h"
#include "app/ui/NamLookAndFeel.h"
#include "app/ui/StackWidgets.h"

using namespace nam::ui;

namespace {
const juce::String kDotSep = juce::String::fromUTF8 ("\xC2\xB7");   // ·

// "1 pedal" / "2 pedals" -- singular for exactly one, plural otherwise.
juce::String countNoun (int n, const char* singular, const char* plural) {
    return juce::String (n) + " " + (n == 1 ? singular : plural);
}
}   // namespace

StacksHomeScreen::StacksHomeScreen () {
    setOpaque (true);
    addChildComponent (wizard_);
}

bool StacksHomeScreen::closeWizard () {
    if (!wizard_.isOpen ()) return false;
    wizard_.close ();
    return true;
}

void StacksHomeScreen::setStacks (std::vector<nam::Stack> stacks, int current) {
    stacks_ = std::move (stacks);
    current_ = current;
    layout ();
    repaint ();
}

void StacksHomeScreen::resized () {
    layout ();
    wizard_.setBounds (getLocalBounds ());
}

juce::String StacksHomeScreen::metaLine (const nam::Stack& st) const {
    int pedals = 0, amps = 0;
    for (const auto& it : st.chain) {
        if (it.type == nam::GearType::Pedal) ++pedals;
        else if (it.type == nam::GearType::Amp) ++amps;
    }
    return countNoun (pedals, "pedal", "pedals") + " " + kDotSep + " " +
           countNoun (amps, "amp", "amps") + " " + kDotSep + " " +
           countNoun ((int)st.scenes.size (), "scene", "scenes");
}

void StacksHomeScreen::layout () {
    // No header row at all: the bottom nav is the only chrome (CLAUDE.md
    // "screens share one chrome grammar"), so the rigs start at the top.
    listArea_ = getLocalBounds ().withTrimmedTop (12).reduced (20, 4);

    // NEW STACK card first, then one card per rig -- all content-local
    // (y=0 at the list's top), same geometry so they read as one family.
    rowRects_.clear ();
    int y = 0;
    constexpr int rowH = 76, gap = 10;
    newCardRect_ = { 0, y, listArea_.getWidth (), rowH };
    y += rowH + gap;
    for (size_t i = 0; i < stacks_.size (); ++i) {
        RowRect rr;
        rr.body = { 0, y, listArea_.getWidth (), rowH };
        rr.performBtn = { listArea_.getWidth () - 108, y + rowH - 34, 92, 24 };
        rowRects_.push_back (rr);
        y += rowH + gap;
    }
    listContentH_ = juce::jmax (0, y - gap);
    scrollY_ = juce::jlimit (0.0f, (float)juce::jmax (0, listContentH_ - listArea_.getHeight ()),
                             scrollY_);
}

void StacksHomeScreen::paint (juce::Graphics& g) {
    paintHeroBackground (g, getLocalBounds ());

    g.saveState ();
    g.reduceClipRegion (listArea_);
    const int dy = listArea_.getY () - (int)scrollY_;

    // NEW STACK: a rig-sized card in accent outline rather than a rig's own
    // fill, so it belongs to the list without ever reading as a saved rig.
    {
        auto card = newCardRect_.translated (listArea_.getX (), dy);
        if (card.getBottom () >= listArea_.getY () && card.getY () <= listArea_.getBottom ()) {
            g.setColour (col::accentA (0.07f));
            g.fillRoundedRectangle (card.toFloat (), 14.0f);
            g.setColour (col::accentA (0.45f));
            g.drawRoundedRectangle (card.toFloat ().reduced (0.5f), 14.0f, 1.2f);

            // "+" glyph drawn as two bars -- a text "+" sits optically high
            // next to tracked caps at this size.
            const auto plus =
                card.reduced (18, 0).removeFromLeft (18).withSizeKeepingCentre (14, 14);
            g.setColour (col::accent);
            g.fillRoundedRectangle ((float)plus.getCentreX () - 1.0f, (float)plus.getY (), 2.0f,
                                    (float)plus.getHeight (), 1.0f);
            g.fillRoundedRectangle ((float)plus.getX (), (float)plus.getCentreY () - 1.0f,
                                    (float)plus.getWidth (), 2.0f, 1.0f);

            auto text = card.reduced (18, 0).withTrimmedLeft (26);
            g.setFont (uiFontTracked (11.0f, true));
            g.setColour (col::accent);
            g.drawText ("NEW STACK", text.removeFromTop (card.getHeight () / 2 + 2),
                        juce::Justification::centredLeft, false);
            g.setFont (uiFont (11.0f, false));
            g.setColour (col::inkA (0.4f));
            g.drawText ("build a rig from a template or from scratch", text,
                        juce::Justification::topLeft, true);
        }
    }

    for (size_t i = 0; i < stacks_.size (); ++i) {
        const auto& st = stacks_[i];
        auto body = rowRects_[i].body.translated (listArea_.getX (), dy);
        if (body.getBottom () < listArea_.getY () || body.getY () > listArea_.getBottom ())
            continue;
        g.setColour (col::inkA (0.02f));
        g.fillRoundedRectangle (body.toFloat (), 14.0f);
        g.setColour (col::inkA (0.12f));
        g.drawRoundedRectangle (body.toFloat ().reduced (0.5f), 14.0f, 1.0f);

        auto in = body.reduced (16, 10);
        auto topRow = in.removeFromTop (in.getHeight () / 2 + 2);
        auto badgeRect = topRow.removeFromRight (66).withSizeKeepingCentre (60, 18);
        g.setFont (uiFont (15.0f, true));
        g.setColour (col::ink);
        g.drawText (juce::String (st.name), topRow, juce::Justification::centredLeft, true);
        drawRoutingBadge (g, badgeRect, st.routing);

        g.setFont (uiFont (11.0f, false));
        g.setColour (col::inkA (0.45f));
        g.drawText (metaLine (st), in, juce::Justification::topLeft, true);

        auto perform = rowRects_[i].performBtn.translated (listArea_.getX (), dy);
        drawPill (g, perform.toFloat (), col::accentA (0.14f), col::accentA (0.5f));
        g.setFont (uiFontTracked (9.0f, true));
        g.setColour (col::accentAlt);
        auto pin = perform.reduced (14, 0);
        drawFwdTriangle (g, pin.removeFromLeft (7).withSizeKeepingCentre (6, 8).toFloat (),
                         col::accentAlt);
        g.drawText ("CONTROLS", pin, juce::Justification::centred, false);
    }
    g.restoreState ();

    if (listContentH_ > listArea_.getHeight ()) {
        const float frac = (float)listArea_.getHeight () / (float)listContentH_;
        const float thumbH = juce::jmax (24.0f, listArea_.getHeight () * frac);
        const float travel = (float)listArea_.getHeight () - thumbH - 8.0f;
        const float pos = scrollY_ / (float)juce::jmax (1, listContentH_ - listArea_.getHeight ());
        g.setColour (col::inkA (0.22f));
        g.fillRoundedRectangle ((float)listArea_.getRight () - 7.0f,
                                (float)listArea_.getY () + 4.0f + travel * pos, 3.0f, thumbH, 1.5f);
    }
}

void StacksHomeScreen::mouseDown (const juce::MouseEvent& e) {
    const auto p = e.getPosition ();
    pressPos_ = p;
    moved_ = false;
    pressedInList_ = !listArea_.isEmpty () && listArea_.contains (p);
    if (pressedInList_) pressScrollY_ = scrollY_;
}

void StacksHomeScreen::mouseDrag (const juce::MouseEvent& e) {
    if (!pressedInList_) return;
    const int dy = e.getPosition ().y - pressPos_.y;
    if (std::abs (dy) > 8) moved_ = true;
    if (!moved_) return;
    scrollY_ = juce::jlimit (0.0f, (float)juce::jmax (0, listContentH_ - listArea_.getHeight ()),
                             pressScrollY_ - (float)dy);
    repaint (listArea_);
}

void StacksHomeScreen::mouseUp (const juce::MouseEvent& e) {
    const bool tap = !moved_ && pressedInList_;
    pressedInList_ = false;
    if (!tap) return;

    const auto p = e.getPosition ();
    if (!listArea_.contains (p)) return;
    const juce::Point<int> cp{ p.x - listArea_.getX (), p.y - listArea_.getY () + (int)scrollY_ };

    if (newCardRect_.contains (cp)) {
        if (onCreate) onCreate ();
        return;
    }
    for (size_t i = 0; i < rowRects_.size (); ++i) {
        const auto& row = rowRects_[i];
        if (row.performBtn.contains (cp)) {
            if (onPerform) onPerform ((int)i);
            return;
        }
        if (row.body.contains (cp)) {
            if (onOpen) onOpen ((int)i);
            return;
        }
    }
}
