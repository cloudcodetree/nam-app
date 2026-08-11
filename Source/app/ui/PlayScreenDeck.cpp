#include "app/ui/PlayScreen.h"
#include "app/ui/NamLookAndFeel.h"

// PlayScreen's non-card deck layouts (detail list / 2-col grid / 4-col
// grid): data setters, shared geometry, and the panel painter. Split out of
// PlayScreen.cpp per the no-god-files rule.

using namespace nam::ui;

void PlayScreen::setDeckItems (std::vector<DeckItem> items) {
    deckItems_ = std::move (items);
    deckScroll_ = juce::jlimit (
        0.0f, (float)juce::jmax (0, deckContentHeight () - artRect_.getHeight () + 20),
        deckScroll_);
    if (layoutMode_ != 0) repaint (artRect_.expanded (12));
}

void PlayScreen::setActiveDeckIndex (int index) {
    if (activeIdx_ == index) return;
    activeIdx_ = index;
    if (layoutMode_ != 0) repaint (artRect_.expanded (12));
}

// List/grid geometry, shared by paint and hit-testing. Rects are in screen
// coords with the current scroll applied.
juce::Rectangle<int> PlayScreen::deckItemRect (int i) const {
    const auto area = artRect_.reduced (10, 10);
    if (layoutMode_ == 1) {
        constexpr int rowH = 68, gap = 6;
        return { area.getX (), area.getY () + i * (rowH + gap) - (int)deckScroll_, area.getWidth (),
                 rowH };
    }
    const int cols = layoutMode_ == 2 ? 2 : 4;
    const int gap = layoutMode_ == 2 ? 8 : 6;
    const int cw = (area.getWidth () - gap * (cols - 1)) / cols;
    return { area.getX () + (i % cols) * (cw + gap),
             area.getY () + (i / cols) * (cw + gap) - (int)deckScroll_, cw, cw };
}

int PlayScreen::deckContentHeight () const {
    const int n = (int)deckItems_.size ();
    if (n == 0) return 0;
    const auto last = deckItemRect (n - 1);
    return last.getBottom () + (int)deckScroll_ - (artRect_.getY () + 10) + 10;
}

void PlayScreen::paintDeckPanel (juce::Graphics& g) {
    // Panel chrome matches the hero card: shadow, rounded clip, wash.
    juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 34, { 0, 18 })
        .drawForRectangle (g, artRect_);
    juce::Path clip;
    clip.addRoundedRectangle (artRect_.toFloat (), 14.0f);
    g.saveState ();
    g.reduceClipRegion (clip);
    juce::ColourGradient bg (col::bgGradTop.brighter (0.05f), (float)artRect_.getCentreX (),
                             (float)artRect_.getY (), col::bg, (float)artRect_.getCentreX (),
                             (float)artRect_.getBottom (), false);
    g.setGradientFill (bg);
    g.fillRect (artRect_);

    if (deckItems_.empty ()) {
        g.setFont (uiFont (13.0f, false));
        g.setColour (col::inkA (0.45f));
        g.drawText ("nothing here yet", artRect_, juce::Justification::centred, false);
        g.restoreState ();
        g.setColour (col::inkA (0.10f));
        g.drawRoundedRectangle (artRect_.toFloat (), 14.0f, 1.0f);
        return;
    }

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    auto thumbOrInitial = [&] (const DeckItem& it, juce::Rectangle<int> rr, float corner,
                               float initialPt) {
        juce::Path tc;
        tc.addRoundedRectangle (rr.toFloat (), corner);
        g.saveState ();
        g.reduceClipRegion (tc);
        if (it.thumb.isValid ()) {
            g.setOpacity (1.0f);   // row washes leave a low-alpha colour behind
            g.drawImageWithin (it.thumb, rr.getX (), rr.getY (), rr.getWidth (), rr.getHeight (),
                               juce::RectanglePlacement::fillDestination);
        } else {
            g.setColour (col::bgGradTop.brighter (0.12f));
            g.fillRect (rr);
            g.setFont (displayFont (initialPt));
            g.setColour (col::inkA (0.25f));
            g.drawText (it.title.substring (0, 1).toUpperCase (), rr, juce::Justification::centred,
                        false);
        }
        g.restoreState ();
    };

    const int n = (int)deckItems_.size ();
    for (int i = 0; i < n; ++i) {
        const auto rr = deckItemRect (i);
        if (rr.getBottom () < artRect_.getY () || rr.getY () > artRect_.getBottom ()) continue;
        const auto& it = deckItems_[(size_t)i];
        const bool active = (i == activeIdx_);

        if (layoutMode_ == 1) {
            // Detail row: thumb / title + sub / heart state.
            g.setColour (active ? col::accentA (0.10f) : col::inkA (0.04f));
            g.fillRoundedRectangle (rr.toFloat (), 10.0f);
            if (active) {
                g.setColour (col::accentA (0.55f));
                g.drawRoundedRectangle (rr.toFloat ().reduced (0.5f), 10.0f, 1.0f);
            }
            auto in = rr.reduced (10, 10);
            thumbOrInitial (it, in.removeFromLeft (in.getHeight ()), 8.0f, 22.0f);
            in.removeFromLeft (12);
            if (it.kept) {
                const auto hb =
                    in.removeFromRight (22).toFloat ().withSizeKeepingCentre (14.0f, 14.0f);
                juce::Path hp;
                hp.startNewSubPath (0.50f, 0.32f);
                hp.cubicTo (0.50f, 0.20f, 0.38f, 0.12f, 0.27f, 0.12f);
                hp.cubicTo (0.11f, 0.12f, 0.04f, 0.26f, 0.04f, 0.38f);
                hp.cubicTo (0.04f, 0.58f, 0.26f, 0.74f, 0.50f, 0.92f);
                hp.cubicTo (0.74f, 0.74f, 0.96f, 0.58f, 0.96f, 0.38f);
                hp.cubicTo (0.96f, 0.26f, 0.89f, 0.12f, 0.73f, 0.12f);
                hp.cubicTo (0.62f, 0.12f, 0.50f, 0.20f, 0.50f, 0.32f);
                hp.closeSubPath ();
                hp.applyTransform (juce::AffineTransform::scale (hb.getWidth (), hb.getHeight ())
                                       .translated (hb.getX (), hb.getY ()));
                g.setColour (col::accent);
                g.fillPath (hp);
            }
            g.setFont (uiFont (13.0f, true));
            g.setColour (active ? col::ink : col::inkA (0.85f));
            g.drawText (it.title, in.removeFromTop (in.getHeight () / 2 + 4),
                        juce::Justification::bottomLeft, false);
            g.setFont (uiFontTracked (8.0f, true));
            g.setColour (col::inkA (0.4f));
            g.drawText (it.sub, in, juce::Justification::topLeft, false);
        } else {
            // Grid cell: cover thumb, bottom scrim + one-line title.
            thumbOrInitial (it, rr, 10.0f, layoutMode_ == 2 ? 34.0f : 20.0f);
            juce::Path tc;
            tc.addRoundedRectangle (rr.toFloat (), 10.0f);
            g.saveState ();
            g.reduceClipRegion (tc);
            const int scrimH = layoutMode_ == 2 ? 40 : 26;
            juce::ColourGradient sc (col::bg.withAlpha (0.9f), (float)rr.getCentreX (),
                                     (float)rr.getBottom (), col::bg.withAlpha (0.0f),
                                     (float)rr.getCentreX (), (float)(rr.getBottom () - scrimH),
                                     false);
            g.setGradientFill (sc);
            g.fillRect (rr);
            g.setFont (uiFont (layoutMode_ == 2 ? 11.0f : 8.0f, true));
            g.setColour (col::inkA (0.9f));
            g.drawText (it.title,
                        rr.reduced (layoutMode_ == 2 ? 10 : 5, 0)
                            .removeFromBottom (layoutMode_ == 2 ? 24 : 16),
                        juce::Justification::centredLeft, false);
            g.restoreState ();
            g.setColour (active ? col::accentA (0.8f) : col::inkA (0.08f));
            g.drawRoundedRectangle (rr.toFloat ().reduced (0.75f), 10.0f, active ? 1.6f : 1.0f);
        }
    }
    g.restoreState ();

    // Scrollbar (overlay rule: content beyond the panel scrolls).
    const int contentH = deckContentHeight ();
    if (contentH > artRect_.getHeight ()) {
        const float frac = (float)artRect_.getHeight () / (float)contentH;
        const float thumbH = juce::jmax (24.0f, artRect_.getHeight () * frac);
        const float travel = (float)artRect_.getHeight () - thumbH - 8.0f;
        const float pos = deckScroll_ / (float)(contentH - artRect_.getHeight ());
        g.setColour (col::inkA (0.22f));
        g.fillRoundedRectangle ((float)artRect_.getRight () - 7.0f,
                                (float)artRect_.getY () + 4.0f +
                                    travel * juce::jlimit (0.0f, 1.0f, pos),
                                3.0f, thumbH, 1.5f);
    }
    g.setColour (col::inkA (0.10f));
    g.drawRoundedRectangle (artRect_.toFloat (), 14.0f, 1.0f);
}
