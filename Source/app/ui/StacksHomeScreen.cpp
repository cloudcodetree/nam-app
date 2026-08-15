#include "app/ui/StacksHomeScreen.h"
#include "app/ui/NamLookAndFeel.h"
#include "app/ui/StackWidgets.h"

using namespace nam::ui;

namespace {
const juce::String kDotSep = juce::String::fromUTF8 ("\xC2\xB7");       // ·
const juce::String kEmDash = juce::String::fromUTF8 ("\xE2\x80\x94");   // —
const juce::String kSubtitle =
    "your rigs " + kEmDash + " pedals, amps, cabs and post, wired for the floor";
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

juce::String StacksHomeScreen::chipLabel (size_t i) const {
    const auto& st = stacks_[i];
    return juce::String ((int)i + 1) + ". " +
           juce::String (st.name).upToFirstOccurrenceOf (kDotSep, false, false);
}

namespace {
// "1 pedal" / "2 pedals" -- singular for exactly one, plural otherwise.
juce::String countNoun (int n, const char* singular, const char* plural) {
    return juce::String (n) + " " + (n == 1 ? singular : plural);
}
}   // namespace

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
    auto b = getLocalBounds ();
    headerRect_ = b.removeFromTop (52);
    gearRect_ = { headerRect_.getRight () - 52, headerRect_.getCentreY () - 21, 42, 42 };

    titleRowRect_ = b.removeFromTop (44).reduced (20, 0);
    newBtnRect_ = titleRowRect_.removeFromRight (116).withSizeKeepingCentre (112, 32);

    // 2 lines' worth of 12pt body text -- was 26px (~1 line), which ran the
    // subtitle off the right edge instead of wrapping/eliding it.
    subtitleRect_ = b.removeFromTop (38).reduced (20, 0);
    b.removeFromTop (8);

    setlistLabelRect_ = b.removeFromTop (18).reduced (20, 0);
    chipsRect_ = b.removeFromTop (40).reduced (20, 0);
    b.removeFromTop (8);

    listArea_ = b.reduced (20, 4);

    // SETLIST chips: content-local (x=0 at the strip's left edge), width
    // sized to fit "{n}. {name}".
    chipRects_.clear ();
    int cx = 0;
    for (size_t i = 0; i < stacks_.size (); ++i) {
        const int w = juce::jmax (
            56,
            (int)juce::GlyphArrangement::getStringWidth (uiFont (11.0f, true), chipLabel (i)) + 28);
        chipRects_.push_back ({ cx, 0, w, chipsRect_.getHeight () });
        cx += w + 8;
    }
    chipsContentW_ = cx;
    chipScrollX_ = juce::jlimit (
        0.0f, (float)juce::jmax (0, chipsContentW_ - chipsRect_.getWidth ()), chipScrollX_);

    // Stack rows: content-local (y=0 at the list's top).
    rowRects_.clear ();
    int y = 0;
    constexpr int rowH = 76, gap = 10;
    for (size_t i = 0; i < stacks_.size (); ++i) {
        RowRect rr;
        rr.body = { 0, y, listArea_.getWidth (), rowH };
        rr.performBtn = { listArea_.getWidth () - 108, y + rowH - 34, 92, 24 };
        rowRects_.push_back (rr);
        y += rowH + gap;
    }
    listContentH_ = y > 0 ? y - gap : 0;
    scrollY_ = juce::jlimit (0.0f, (float)juce::jmax (0, listContentH_ - listArea_.getHeight ()),
                             scrollY_);
}

void StacksHomeScreen::paint (juce::Graphics& g) {
    paintHeroBackground (g, getLocalBounds ());
    drawStacksBrandHeader (g, headerRect_, gearRect_);

    g.setFont (displayFont (26.0f));
    g.setColour (col::ink);
    g.drawText ("Stacks", titleRowRect_, juce::Justification::centredLeft, false);
    drawPill (g, newBtnRect_.toFloat (), col::accent, col::accent);
    g.setFont (uiFontTracked (10.0f, true));
    g.setColour (col::inkOnAccent);
    g.drawText ("+ NEW STACK", newBtnRect_, juce::Justification::centred, false);

    g.setFont (uiFont (12.0f, false));
    g.setColour (col::inkA (0.45f));
    g.drawFittedText (kSubtitle, subtitleRect_, juce::Justification::topLeft, 2, 1.0f);

    g.setFont (uiFontTracked (9.0f, true));
    g.setColour (col::inkA (0.4f));
    g.drawText ("SETLIST", setlistLabelRect_, juce::Justification::centredLeft, false);

    if (!stacks_.empty ()) {
        g.saveState ();
        g.reduceClipRegion (chipsRect_);
        const int cdx = chipsRect_.getX () - (int)chipScrollX_;
        for (size_t i = 0; i < stacks_.size (); ++i) {
            auto r = chipRects_[i].translated (cdx, chipsRect_.getY ());
            if (r.getRight () < chipsRect_.getX () || r.getX () > chipsRect_.getRight ()) continue;
            drawSetlistChip (g, r, chipLabel (i), (int)i == current_);
        }
        g.restoreState ();
    }

    g.saveState ();
    g.reduceClipRegion (listArea_);
    if (stacks_.empty ()) {
        auto box = listArea_.withHeight (juce::jmin (140, listArea_.getHeight ()));
        juce::Path outline, dashed;
        outline.addRoundedRectangle (box.toFloat (), 14.0f);
        float dashLengths[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dashLengths, 2);
        g.setColour (col::inkA (0.18f));
        g.fillPath (dashed);
        g.setColour (col::inkA (0.45f));
        g.setFont (uiFont (13.0f, false));
        g.drawText ("no stacks yet " + kDotSep + " " + kSubtitle, box.reduced (28, 0),
                    juce::Justification::centred, true);
    } else {
        const int dy = listArea_.getY () - (int)scrollY_;
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
            g.drawText ("PERFORM", pin, juce::Justification::centred, false);
        }
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
    pressRegion_ = Region::None;

    if (gearRect_.expanded (4).contains (p)) {
        if (onSettings) onSettings ();
        return;
    }
    if (newBtnRect_.expanded (4).contains (p)) {
        if (onCreate) onCreate ();
        return;
    }
    if (stacks_.empty ()) return;
    if (chipsRect_.contains (p)) {
        pressRegion_ = Region::Chips;
        chipPressScrollX_ = chipScrollX_;
        return;
    }
    if (listArea_.contains (p)) {
        pressRegion_ = Region::List;
        pressScrollY_ = scrollY_;
    }
}

void StacksHomeScreen::mouseDrag (const juce::MouseEvent& e) {
    const auto p = e.getPosition ();
    if (pressRegion_ == Region::Chips) {
        const int dx = p.x - pressPos_.x;
        if (std::abs (dx) > 6) moved_ = true;
        if (moved_) {
            chipScrollX_ =
                juce::jlimit (0.0f, (float)juce::jmax (0, chipsContentW_ - chipsRect_.getWidth ()),
                              chipPressScrollX_ - (float)dx);
            repaint (chipsRect_);
        }
        return;
    }
    if (pressRegion_ == Region::List) {
        const int dy = p.y - pressPos_.y;
        if (std::abs (dy) > 8) moved_ = true;
        if (moved_) {
            scrollY_ =
                juce::jlimit (0.0f, (float)juce::jmax (0, listContentH_ - listArea_.getHeight ()),
                              pressScrollY_ - (float)dy);
            repaint (listArea_);
        }
    }
}

void StacksHomeScreen::mouseUp (const juce::MouseEvent& e) {
    const auto p = e.getPosition ();
    const bool tap = !moved_;
    const Region region = pressRegion_;
    pressRegion_ = Region::None;
    if (!tap) return;

    if (region == Region::Chips) {
        const juce::Point<int> cp{ p.x - chipsRect_.getX () + (int)chipScrollX_,
                                   p.y - chipsRect_.getY () };
        for (size_t i = 0; i < chipRects_.size (); ++i)
            if (chipRects_[i].contains (cp)) {
                if (onSetCurrent) onSetCurrent ((int)i);
                return;
            }
        return;
    }
    if (region == Region::List) {
        const juce::Point<int> cp{ p.x - listArea_.getX (),
                                   p.y - listArea_.getY () + (int)scrollY_ };
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
}
