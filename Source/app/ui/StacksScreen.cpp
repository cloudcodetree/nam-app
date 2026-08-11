#include "app/ui/StacksScreen.h"
#include "app/ui/NamLookAndFeel.h"

#include <cmath>

using namespace nam::ui;

const std::array<StacksScreen::SlotDef, StacksScreen::kNumSlots>& StacksScreen::slotDefs () {
    static const std::array<SlotDef, kNumSlots> defs{ {
        { "AMP", "amp" },
        { "CABINET", "ir" },   // cab slot lists IRs (format, not gear)
        { "PEDAL", "pedal" },
        { "OUTBOARD", "outboard" },
        { "SPACES", "space" },
        { "EXPERIMENTAL", "experimental" },
    } };
    return defs;
}

StacksScreen::StacksScreen () { setOpaque (true); }

void StacksScreen::setStacks (std::vector<Stack> stacks, int selected) {
    stacks_ = std::move (stacks);
    selected_ = selected;
    pickerSlot_ = -1;
    layout ();
    repaint ();
}

void StacksScreen::resized () { layout (); }

void StacksScreen::layout () {
    auto r = getLocalBounds ();
    auto top = r.removeFromTop (juce::jmax (52, r.getHeight () / 15));
    newBtnRect_ = { top.getRight () - 132, top.getCentreY () - 17, 112, 34 };
    listArea_ = r.reduced (24, 8);

    rows_.assign (stacks_.size (), {});
    slotRects_.fill ({});
    const int rowH = 60, slotH = 46, gap = 10;
    int y = 0;
    for (size_t i = 0; i < stacks_.size (); ++i) {
        auto& row = rows_[i];
        row.header = { listArea_.getX (), y, listArea_.getWidth (), rowH };
        row.deleteBtn = { row.header.getRight () - 40, y + rowH / 2 - 15, 30, 30 };
        row.applyBtn = { row.deleteBtn.getX () - 96, y + rowH / 2 - 16, 88, 32 };
        y += rowH;
        if ((int)i == selected_) {
            for (int s = 0; s < kNumSlots; ++s) {
                slotRects_[(size_t)s] = { listArea_.getX () + 12, y + 4, listArea_.getWidth () - 24,
                                          slotH - 8 };
                y += slotH;
            }
            y += 6;
        }
        y += gap;
    }
    contentH_ = y;
    scrollY_ =
        juce::jlimit (0.0f, (float)juce::jmax (0, contentH_ - listArea_.getHeight ()), scrollY_);
}

void StacksScreen::paint (juce::Graphics& g) {
    paintHeroBackground (g, getLocalBounds ());
    auto text = [&] (const juce::String& s, juce::Font f, juce::Colour c, juce::Rectangle<int> rr,
                     juce::Justification j) {
        g.setFont (f);
        g.setColour (c);
        g.drawText (s, rr, j, false);
    };

    text ("Stacks", displayFont (26.0f), col::ink, { listArea_.getX (), 8, 220, 44 },
          juce::Justification::centredLeft);
    drawPill (g, newBtnRect_.toFloat (), col::accent, col::accent);
    text ("+ NEW STACK", uiFontTracked (10.0f, true), col::inkOnAccent, newBtnRect_,
          juce::Justification::centred);

    g.saveState ();
    g.reduceClipRegion (listArea_);
    const int dy = listArea_.getY () - (int)scrollY_;
    auto S = [dy] (juce::Rectangle<int> rr) { return rr.translated (0, dy); };

    if (stacks_.empty ()) {
        text ("no stacks yet " + juce::String::fromUTF8 ("\xC2\xB7") +
                  " build a rig from live TONE3000 gear",
              uiFont (13.0f, false), col::inkA (0.45f), listArea_, juce::Justification::centred);
    }

    for (size_t i = 0; i < stacks_.size (); ++i) {
        const auto& row = rows_[i];
        const auto& st = stacks_[i];
        const bool open = ((int)i == selected_);
        auto header = S (row.header);
        if (header.getBottom () >= listArea_.getY () && header.getY () <= listArea_.getBottom ()) {
            g.setColour (open ? col::accentA (0.06f) : col::inkA (0.02f));
            g.fillRoundedRectangle (header.reduced (0, 4).toFloat (), 12.0f);
            g.setColour (open ? col::accentA (0.4f) : col::inkA (0.1f));
            g.drawRoundedRectangle (header.reduced (0, 4).toFloat ().reduced (0.5f), 12.0f, 1.0f);
            int filled = 0;
            for (const auto& id : st.toneIds) filled += id.isNotEmpty () ? 1 : 0;
            text (st.name, uiFont (15.0f, true), col::ink,
                  header.reduced (16, 0).withTrimmedRight (150), juce::Justification::centredLeft);
            text (juce::String (filled) + "/" + juce::String ((int)kNumSlots),
                  uiFont (11.0f, false), col::inkA (0.4f),
                  { header.getX () + 16, header.getCentreY () + 6, 60, 16 },
                  juce::Justification::centredLeft);
            drawPill (g, S (row.applyBtn).toFloat (),
                      filled > 0 ? col::accentA (0.12f) : juce::Colours::transparentBlack,
                      filled > 0 ? col::accentA (0.6f) : col::inkA (0.16f));
            text ("LOAD", uiFontTracked (10.0f, true),
                  filled > 0 ? col::accentAlt : col::inkA (0.35f), S (row.applyBtn),
                  juce::Justification::centred);
            text (juce::String::fromUTF8 ("\xC3\x97"), uiFont (16.0f, false), col::inkA (0.4f),
                  S (row.deleteBtn), juce::Justification::centred);
        }
        if (open) {
            for (int s = 0; s < kNumSlots; ++s) {
                auto sr = S (slotRects_[(size_t)s]);
                if (sr.getBottom () < listArea_.getY () || sr.getY () > listArea_.getBottom ())
                    continue;
                g.setColour (col::inkA (0.04f));
                g.fillRoundedRectangle (sr.toFloat (), 10.0f);
                g.setColour (col::inkA (0.14f));
                g.drawRoundedRectangle (sr.toFloat ().reduced (0.5f), 10.0f, 1.0f);
                auto in = sr.reduced (12, 4);
                text (slotDefs ()[(size_t)s].label, uiFontTracked (8.0f, true), col::inkA (0.4f),
                      in.removeFromTop (13), juce::Justification::centredLeft);
                auto val = in;
                text (juce::String::fromUTF8 ("\xE2\x96\xBE"), uiFont (10.0f, false),
                      col::inkA (0.5f), val.removeFromRight (16), juce::Justification::centred);
                const bool has = st.toneIds[(size_t)s].isNotEmpty ();
                text (has ? st.titles[(size_t)s] : juce::String ("None"), uiFont (12.0f, true),
                      has ? col::ink : col::inkA (0.4f), val, juce::Justification::centredLeft);
            }
        }
    }
    g.restoreState ();

    // Slot picker overlay (house style: anchored panel, capped, scrollable).
    if (pickerSlot_ >= 0) {
        g.setColour (juce::Colour (0xf214101f));
        g.fillRoundedRectangle (pickerRect_.toFloat (), 14.0f);
        g.setColour (col::inkA (0.18f));
        g.drawRoundedRectangle (pickerRect_.toFloat ().reduced (0.5f), 14.0f, 1.0f);
        text (juce::String (slotDefs ()[(size_t)pickerSlot_].label) +
                  juce::String::fromUTF8 (" \xc2\xb7 TONE3000"),
              uiFontTracked (9.0f, true), col::inkA (0.4f),
              { pickerRect_.getX () + 18, pickerRect_.getY () + 10, pickerRect_.getWidth () - 36,
                16 },
              juce::Justification::centredLeft);
        g.saveState ();
        juce::Path clip;
        clip.addRoundedRectangle (pickerRect_.toFloat ().reduced (1.0f).withTrimmedTop (30.0f),
                                  13.0f);
        g.reduceClipRegion (clip);
        constexpr int rowH = 40, titleH = 34, pad = 8;
        if (pickerLoading_) {
            text ("loading" + juce::String::fromUTF8 ("\xE2\x80\xA6"), uiFont (13.0f, false),
                  col::inkA (0.5f), pickerRect_, juce::Justification::centred);
        } else if (pickerTitles_.isEmpty ()) {
            text ("nothing found", uiFont (13.0f, false), col::inkA (0.5f), pickerRect_,
                  juce::Justification::centred);
        }
        for (int i = 0; i < pickerTitles_.size (); ++i) {
            const juce::Rectangle<int> row (pickerRect_.getX () + 6,
                                            pickerRect_.getY () + titleH + pad + i * rowH -
                                                (int)pickerScroll_,
                                            pickerRect_.getWidth () - 12, rowH);
            if (row.getBottom () < pickerRect_.getY () || row.getY () > pickerRect_.getBottom ())
                continue;
            text (pickerTitles_[i], uiFont (13.0f, false), col::inkA (0.85f), row.reduced (12, 0),
                  juce::Justification::centredLeft);
        }
        g.restoreState ();
        if (pickerContentH_ > pickerRect_.getHeight ()) {
            const float frac = (float)pickerRect_.getHeight () / (float)pickerContentH_;
            const float thumbH = juce::jmax (24.0f, pickerRect_.getHeight () * frac);
            const float travel = (float)pickerRect_.getHeight () - thumbH - 8.0f;
            const float pos =
                pickerScroll_ / (float)juce::jmax (1, pickerContentH_ - pickerRect_.getHeight ());
            g.setColour (col::inkA (0.25f));
            g.fillRoundedRectangle ((float)pickerRect_.getRight () - 7.0f,
                                    (float)pickerRect_.getY () + 4.0f + travel * pos, 3.0f, thumbH,
                                    1.5f);
        }
    }
}

void StacksScreen::openPicker (int slot) {
    pickerSlot_ = slot;
    pickerLoading_ = true;
    pickerIds_.clear ();
    pickerTitles_.clear ();
    pickerFormats_.clear ();
    pickerScroll_ = 0.0f;
    // Height-capped panel centred over the list (house overlay rule).
    const int w = juce::jmin (360, getWidth () - 40);
    const int h = juce::jmin (listArea_.getHeight () - 20, getHeight () * 55 / 100);
    pickerRect_ = { getWidth () / 2 - w / 2, listArea_.getY () + 10, w, h };
    pickerContentH_ = 0;
    repaint ();
    if (onFetchSlotOptions)
        onFetchSlotOptions (slot, [this, slot] (juce::StringArray ids, juce::StringArray titles,
                                                juce::StringArray formats) {
            if (pickerSlot_ != slot) return;   // closed / switched meanwhile
            pickerIds_ = std::move (ids);
            pickerTitles_ = std::move (titles);
            pickerFormats_ = std::move (formats);
            pickerLoading_ = false;
            constexpr int rowH = 40, titleH = 34, pad = 8;
            pickerContentH_ = titleH + pad + rowH * pickerTitles_.size () + pad;
            repaint ();
        });
}

void StacksScreen::mouseDown (const juce::MouseEvent& e) {
    const auto p = e.getPosition ();
    pressPos_ = p;

    if (pickerSlot_ >= 0) {
        if (pickerRect_.contains (p)) {
            pickerPressed_ = true;
            pickerMoved_ = false;
            pickerPressScroll_ = pickerScroll_;
        } else {
            pickerSlot_ = -1;
            repaint ();
        }
        return;
    }

    if (newBtnRect_.expanded (4).contains (p)) {
        if (onCreate) onCreate ();
        return;
    }

    if (listArea_.contains (p)) {
        listPressed_ = true;
        listMoved_ = false;
        pressScrollY_ = scrollY_;
    }
}

void StacksScreen::mouseDrag (const juce::MouseEvent& e) {
    const int dy = e.getPosition ().y - pressPos_.y;
    if (pickerPressed_) {
        if (std::abs (dy) > 8) pickerMoved_ = true;
        if (pickerMoved_) {
            pickerScroll_ = juce::jlimit (
                0.0f, (float)juce::jmax (0, pickerContentH_ - pickerRect_.getHeight ()),
                pickerPressScroll_ - (float)dy);
            repaint (pickerRect_.expanded (2));
        }
        return;
    }
    if (listPressed_) {
        if (std::abs (dy) > 8) listMoved_ = true;
        if (listMoved_) {
            scrollY_ =
                juce::jlimit (0.0f, (float)juce::jmax (0, contentH_ - listArea_.getHeight ()),
                              pressScrollY_ - (float)dy);
            repaint ();
        }
    }
}

void StacksScreen::mouseUp (const juce::MouseEvent& e) {
    const auto p = e.getPosition ();

    if (pickerPressed_) {
        if (!pickerMoved_ && !pickerLoading_) {
            constexpr int rowH = 40, titleH = 34, pad = 8;
            const int i = (p.y - pickerRect_.getY () - titleH - pad + (int)pickerScroll_) / rowH;
            if (i >= 0 && i < pickerTitles_.size () && selected_ >= 0) {
                if (onSlotPicked)
                    onSlotPicked (selected_, pickerSlot_, pickerIds_[i], pickerTitles_[i],
                                  i < pickerFormats_.size () ? pickerFormats_[i] : juce::String ());
                pickerSlot_ = -1;
                repaint ();
            }
        }
        pickerPressed_ = pickerMoved_ = false;
        return;
    }

    if (!listPressed_) return;
    const bool tap = !listMoved_;
    listPressed_ = listMoved_ = false;
    if (!tap) return;

    const juce::Point<int> cp{ p.x, p.y - listArea_.getY () + (int)scrollY_ };
    for (size_t i = 0; i < rows_.size (); ++i) {
        const auto& row = rows_[i];
        if (row.deleteBtn.contains (cp)) {
            if (onDelete) onDelete ((int)i);
            return;
        }
        if (row.applyBtn.contains (cp)) {
            if (onApply) onApply ((int)i);
            return;
        }
        if (row.header.contains (cp)) {
            if (onSelect) onSelect ((int)i);
            return;
        }
    }
    if (selected_ >= 0)
        for (int s = 0; s < kNumSlots; ++s)
            if (slotRects_[(size_t)s].contains (cp)) {
                openPicker (s);
                return;
            }
}
