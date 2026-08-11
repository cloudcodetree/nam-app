#include "app/ui/LiveScreen.h"
#include "app/ui/NamLookAndFeel.h"

using namespace nam::ui;

namespace {
const juce::String kNdash = juce::String::fromUTF8 ("\xE2\x80\x93");   // –
const juce::String kDot = juce::String::fromUTF8 ("\xC2\xB7");         // ·
}   // namespace

LiveScreen::LiveScreen () { setOpaque (true); }

void LiveScreen::setSlots (std::vector<nam::LibraryEntry> slots) {
    slots_ = std::move (slots);
    if (active_ >= (int)slots_.size ()) active_ = 0;
    relayout ();
    repaint ();
}

void LiveScreen::resized () { relayout (); }

void LiveScreen::relayout () {
    auto r = getLocalBounds ();
    exitRect_ = { getWidth () - 20 - 72, 26, 72, 34 };
    r.removeFromTop (74);
    auto footer = r.removeFromBottom (56);
    juce::ignoreUnused (footer);
    auto list = r.reduced (16, 6);

    rowRects_.clear ();
    const int n = juce::jmax (1, (int)slots_.size ());
    const int gap = 10;
    const int rowH = juce::jlimit (56, 120, (list.getHeight () - (n - 1) * gap) / n);
    int y = list.getY ();
    for (int i = 0; i < (int)slots_.size (); ++i) {
        rowRects_.push_back ({ list.getX (), y, list.getWidth (), rowH });
        y += rowH + gap;
    }
}

void LiveScreen::paint (juce::Graphics& g) {
    paintHeroBackground (g, getLocalBounds (), false);
    auto text = [&] (const juce::String& s, juce::Font f, juce::Colour c, juce::Rectangle<int> rr,
                     juce::Justification j) {
        g.setFont (f);
        g.setColour (c);
        g.drawText (s, rr, j, false);
    };

    text ("LIVE MODE", uiFontTracked (10.0f, true), col::accentAlt, { 20, 22, 200, 16 },
          juce::Justification::topLeft);
    text ("Setlist", displayFont (28.0f), col::ink, { 20, 34, 240, 40 },
          juce::Justification::topLeft);
    drawPill (g, exitRect_.toFloat (), col::inkA (0.0f), col::inkA (0.22f));
    text ("EXIT", uiFontTracked (12.0f, true), col::inkA (0.7f), exitRect_,
          juce::Justification::centred);

    if (slots_.empty ()) {
        text (juce::String::fromUTF8 ("No tones yet \xE2\x80\x94 keep some from Radio or Library"),
              uiFont (14.0f, false), col::inkA (0.5f), getLocalBounds (),
              juce::Justification::centred);
        return;
    }

    for (int i = 0; i < (int)rowRects_.size (); ++i) {
        const bool on = (i == active_);
        auto row = rowRects_[(size_t)i];
        g.setColour (on ? col::accentA (0.09f) : col::inkA (0.02f));
        g.fillRoundedRectangle (row.toFloat (), 18.0f);
        g.setColour (on ? col::accentA (0.6f) : col::inkA (0.12f));
        g.drawRoundedRectangle (row.toFloat ().reduced (0.5f), 18.0f, 1.0f);

        auto inner = row.reduced (22, 0);
        text (juce::String (i + 1), displayFont (34.0f), on ? col::accentAlt : col::inkA (0.35f),
              inner.removeFromLeft (44), juce::Justification::centredLeft);
        if (on) {
            g.setColour (col::accent);
            g.fillEllipse ((float)inner.getRight () - 14, (float)inner.getCentreY () - 5, 10, 10);
        }
        text (juce::String (slots_[(size_t)i].displayName), uiFont (20.0f, true),
              on ? col::ink : col::inkA (0.6f), inner.withTrimmedRight (24),
              juce::Justification::centredLeft);
    }

    text ("footswitch / MIDI: next" + kNdash + "prev " + kDot + " screen stays awake",
          uiFont (12.0f, false), col::inkA (0.4f),
          getLocalBounds ().removeFromBottom (52).withTrimmedLeft (20).withWidth (getWidth () - 90),
          juce::Justification::centredLeft);
    text ("3.2 ms", uiFont (12.0f, true), col::inkA (0.55f),
          getLocalBounds ().removeFromBottom (52).withTrimmedRight (20),
          juce::Justification::centredRight);
}

void LiveScreen::mouseDown (const juce::MouseEvent& e) {
    const auto p = e.getPosition ();
    if (exitRect_.expanded (8).contains (p)) {
        if (onExit) onExit ();
        return;
    }
    for (int i = 0; i < (int)rowRects_.size (); ++i)
        if (rowRects_[(size_t)i].contains (p)) {
            active_ = i;
            repaint ();
            if (onSelect) onSelect (slots_[(size_t)i]);
            return;
        }
}
