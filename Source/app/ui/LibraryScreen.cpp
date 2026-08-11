#include "app/ui/LibraryScreen.h"
#include "app/ui/NamLookAndFeel.h"

using namespace nam::ui;

namespace {
const juce::String kBack = juce::String::fromUTF8 ("\xE2\x80\xB9");   // ‹
}

LibraryScreen::LibraryScreen () { setOpaque (true); }

void LibraryScreen::setEntries (std::vector<nam::LibraryEntry> entries) {
    all_ = std::move (entries);
    applyFilter ();
    relayout ();
    repaint ();
}

void LibraryScreen::applyFilter () {
    shown_.clear ();
    for (auto& e : all_)
        if (filter_ == 0 || e.favorite) shown_.push_back (e);
}

void LibraryScreen::resized () { relayout (); }

void LibraryScreen::relayout () {
    auto r = getLocalBounds ();
    backRect_ = { 12, 22, 44, 40 };
    r.removeFromTop (72);
    r.removeFromBottom (40);   // footer hint
    filterRow_ = r.removeFromTop (44).reduced (20, 6);
    auto grid = r.reduced (20, 4);

    filterRects_.clear ();
    const char* names[] = { "All", "Favorites" };
    int fx = filterRow_.getX ();
    for (auto* n : names) {
        const int w = (int)juce::String (n).length () * 9 + 28;
        filterRects_.push_back ({ fx, filterRow_.getY (), w, filterRow_.getHeight () });
        fx += w + 8;
    }

    cardRects_.clear ();
    const int cols = 2, gap = 12;
    const int cardW = (grid.getWidth () - gap) / cols;
    const int cardH = cardW + 40;   // square art + label strip
    int i = 0;
    for (int y = grid.getY (); y + cardH <= grid.getBottom () + cardH;) {
        for (int c = 0; c < cols; ++c) {
            if (i >= (int)shown_.size () + 1) break;   // +1 for the import card
            cardRects_.push_back ({ grid.getX () + c * (cardW + gap), y, cardW, cardH });
            ++i;
        }
        if (i >= (int)shown_.size () + 1) break;
        y += cardH + gap;
    }
}

void LibraryScreen::paint (juce::Graphics& g) {
    paintHeroBackground (g, getLocalBounds ());
    auto text = [&] (const juce::String& s, juce::Font f, juce::Colour c, juce::Rectangle<int> rr,
                     juce::Justification j) {
        g.setFont (f);
        g.setColour (c);
        g.drawText (s, rr, j, false);
    };

    text (kBack, uiFont (20.0f, false), col::inkA (0.6f), backRect_, juce::Justification::centred);
    text ("Library", displayFont (30.0f), col::ink, { 44, 20, 200, 44 },
          juce::Justification::centredLeft);
    text (juce::String ((int)all_.size ()) + " tones kept", uiFont (12.0f, false),
          col::inkA (0.45f), { getWidth () - 160, 28, 140, 28 }, juce::Justification::centredRight);

    const char* fnames[] = { "All", "Favorites" };
    for (int i = 0; i < (int)filterRects_.size (); ++i) {
        const bool on = (i == filter_);
        drawPill (g, filterRects_[(size_t)i].toFloat (),
                  on ? col::accentA (0.12f) : col::accentA (0.0f),
                  on ? col::accentA (0.6f) : col::inkA (0.18f));
        text (fnames[i], uiFont (12.0f, false), on ? col::accentAlt : col::inkA (0.7f),
              filterRects_[(size_t)i], juce::Justification::centred);
    }

    for (int i = 0; i < (int)cardRects_.size (); ++i) {
        auto card = cardRects_[(size_t)i];
        const bool isImport = (i == (int)shown_.size ());
        if (isImport) {
            g.setColour (col::inkA (0.25f));
            g.drawRoundedRectangle (card.toFloat ().reduced (0.5f), 14.0f, 1.0f);   // dashed-ish
            text ("+", displayFont (30.0f), col::inkA (0.5f), card.withTrimmedBottom (24),
                  juce::Justification::centred);
            text ("import .nam / IR", uiFont (11.0f, true), col::inkA (0.45f),
                  card.removeFromBottom (28), juce::Justification::centred);
            continue;
        }
        const auto& e = shown_[(size_t)i];
        auto art = card.withHeight (card.getWidth ());
        // art placeholder
        juce::Path clip;
        clip.addRoundedRectangle (art.toFloat (), 12.0f);
        g.saveState ();
        g.reduceClipRegion (clip);
        juce::ColourGradient ag (col::bgGradTop.brighter (0.05f), (float)art.getCentreX (),
                                 (float)art.getY (), col::bg, (float)art.getCentreX (),
                                 (float)art.getBottom (), false);
        g.setGradientFill (ag);
        g.fillRect (art);
        text (juce::String (e.displayName).substring (0, 1).toUpperCase (),
              displayFont (art.getHeight () * 0.5f), col::inkA (0.12f), art,
              juce::Justification::centred);
        g.restoreState ();
        g.setColour (e.favorite ? col::accentA (0.5f) : col::inkA (0.1f));
        g.drawRoundedRectangle (art.toFloat ().reduced (0.5f), 12.0f, 1.0f);
        // label strip
        auto strip = card.withTop (art.getBottom () + 6);
        text (juce::String (e.displayName), displayFont (15.0f), col::ink,
              strip.removeFromTop (20).withTrimmedLeft (2), juce::Justification::topLeft);
        text (e.arch.empty () ? juce::String ("model") : ("A" + juce::String (e.arch)),
              uiFont (11.0f, false), col::inkA (0.45f), strip.withTrimmedLeft (2),
              juce::Justification::topLeft);
    }

    text ("tap plays instantly", uiFont (12.0f, false), col::inkA (0.4f),
          getLocalBounds ().removeFromBottom (36), juce::Justification::centred);
}

void LibraryScreen::mouseDown (const juce::MouseEvent& e) {
    const auto p = e.getPosition ();
    if (backRect_.expanded (10).contains (p)) {
        if (onBack) onBack ();
        return;
    }
    for (int i = 0; i < (int)filterRects_.size (); ++i)
        if (filterRects_[(size_t)i].contains (p)) {
            filter_ = i;
            applyFilter ();
            relayout ();
            repaint ();
            return;
        }
    for (int i = 0; i < (int)cardRects_.size () && i < (int)shown_.size (); ++i)
        if (cardRects_[(size_t)i].contains (p)) {
            if (onLoad) onLoad (shown_[(size_t)i]);
            return;
        }
}
