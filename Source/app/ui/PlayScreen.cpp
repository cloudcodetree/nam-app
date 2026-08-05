#include "app/ui/PlayScreen.h"
#include "app/ui/NamLookAndFeel.h"

using namespace nam::ui;

PlayScreen::PlayScreen() {
    tones_ = {
        { "Plexi '68 Crunch", "Marshall family", "ToneHub" },
        { "Deluxe '65 Clean",  "Fender family",   "AmpCaptures" },
        { "5150 Lead",         "Hi-gain family",  "DjentLab" },
        { "AC30 Top Boost",    "Vox family",      "BritTone" },
    };
    setOpaque (true);
}

void PlayScreen::setLevels (float in, float out) {
    inLevel_  = juce::jlimit (0.0f, 1.0f, in);
    outLevel_ = juce::jlimit (0.0f, 1.0f, out);
    repaint (metersRow_);
}

void PlayScreen::resized() { layout(); }

void PlayScreen::layout() {
    auto r = getLocalBounds();
    topBar_  = r.removeFromTop (juce::jmax (56, r.getHeight() / 14));
    navBar_  = r.removeFromBottom (juce::jmax (72, r.getHeight() / 11));
    metersRow_ = r.removeFromBottom (juce::jmax (78, r.getHeight() / 9)).reduced (20, 6);
    hero_    = r.reduced (26, 4);

    // Top bar: LIBRARY pill (left). CAB badge painted at right.
    libRect_ = { topBar_.getX() + 20, topBar_.getCentreY() - 16, 92, 32 };

    // Hero vertical stack: art (square) / text / transport, centred.
    const int artSize = juce::jmin (hero_.getWidth(), (int) (hero_.getHeight() * 0.48f));
    const int textH = 100, transH = 52, gap = 18;
    const int stackH = artSize + gap + textH + gap + transH;
    const int top = hero_.getY() + juce::jmax (0, (hero_.getHeight() - stackH) / 2);
    artRect_       = { hero_.getCentreX() - artSize / 2, top, artSize, artSize };
    textRect_      = { hero_.getX(), artRect_.getBottom() + gap, hero_.getWidth(), textH };
    transportRect_ = { hero_.getX(), textRect_.getBottom() + gap, hero_.getWidth(), transH };

    prevRect_ = { transportRect_.getX(), transportRect_.getY(), 52, 52 };
    nextRect_ = { transportRect_.getRight() - 52, transportRect_.getY(), 52, 52 };
    progressRect_ = { prevRect_.getRight() + 14, transportRect_.getCentreY() - 1,
                      nextRect_.getX() - 14 - (prevRect_.getRight() + 14), 2 };

    const int nw = navBar_.getWidth() / 4;
    for (int i = 0; i < 4; ++i)
        navRects_[(size_t) i] = { navBar_.getX() + i * nw, navBar_.getY(), nw, navBar_.getHeight() };
}

void PlayScreen::paint (juce::Graphics& g) {
    paintHeroBackground (g, getLocalBounds());

    auto text = [&] (const juce::String& s, juce::Font f, juce::Colour c,
                     juce::Rectangle<int> rr, juce::Justification j) {
        g.setFont (f); g.setColour (c); g.drawText (s, rr, j, false);
    };

    // --- Top bar --------------------------------------------------------
    drawPill (g, libRect_.toFloat(), juce::Colours::transparentBlack, col::inkA (0.22f));
    text ("LIBRARY", uiFontTracked (12.0f, true), col::ink, libRect_, juce::Justification::centred);

    juce::Rectangle<int> cab { topBar_.getRight() - 20 - 96, topBar_.getCentreY() - 16, 96, 32 };
    drawPill (g, cab.toFloat(), col::accentA (0.08f), col::accentA (0.5f));
    text (juce::String::fromUTF8 ("\xF0\x9F\x94\x92") + " CAB",
          uiFontTracked (11.0f, true), col::accentAlt, cab, juce::Justification::centred);

    // --- Hero artwork placeholder --------------------------------------
    {
        juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 34,
                          { 0, 18 }).drawForRectangle (g, artRect_);
        juce::Path clip; clip.addRoundedRectangle (artRect_.toFloat(), 14.0f);
        g.saveState();
        g.reduceClipRegion (clip);
        juce::ColourGradient ag (col::bgGradTop.brighter (0.06f), (float) artRect_.getCentreX(),
                                 (float) artRect_.getY(), col::bg, (float) artRect_.getCentreX(),
                                 (float) artRect_.getBottom(), false);
        g.setGradientFill (ag); g.fillRect (artRect_);
        juce::ColourGradient glow (col::accentA (0.16f), (float) artRect_.getCentreX(),
                                   (float) artRect_.getBottom(), col::accent.withAlpha (0.0f),
                                   (float) artRect_.getCentreX(), (float) artRect_.getY(), false);
        g.setGradientFill (glow); g.fillRect (artRect_);
        // big faint family initial as "album art"
        text (tones_[(size_t) index_].family.substring (0, 1).toUpperCase(),
              displayFont (artRect_.getHeight() * 0.62f), col::inkA (0.10f),
              artRect_, juce::Justification::centred);
        g.restoreState();
        g.setColour (col::inkA (0.10f));
        g.drawRoundedRectangle (artRect_.toFloat(), 14.0f, 1.0f);
    }

    // --- Tone text ------------------------------------------------------
    {
        const auto& t = tones_[(size_t) index_];
        auto tr = textRect_;
        text (t.family.toUpperCase(), uiFontTracked (13.0f, false), col::inkA (0.45f),
              tr.removeFromTop (20), juce::Justification::topLeft);
        text (t.name, displayFont (40.0f), col::ink,
              tr.removeFromTop (52), juce::Justification::topLeft);
        text ("by " + t.author, uiFont (13.0f, false), col::inkA (0.5f),
              tr.removeFromTop (24), juce::Justification::topLeft);
    }

    // --- Transport ------------------------------------------------------
    auto circleBtn = [&] (juce::Rectangle<int> rr, const juce::String& glyph) {
        g.setColour (col::inkA (0.25f));
        g.drawEllipse (rr.toFloat().reduced (0.5f), 1.0f);
        text (glyph, uiFont (20.0f, false), col::ink, rr, juce::Justification::centred);
    };
    circleBtn (prevRect_, juce::String::fromUTF8 ("\xE2\x80\xB9")); // ‹
    circleBtn (nextRect_, juce::String::fromUTF8 ("\xE2\x80\xBA")); // ›
    g.setColour (col::inkA (0.14f));
    g.fillRoundedRectangle (progressRect_.toFloat(), 1.0f);
    const float prog = (float) (index_ + 1) / (float) tones_.size();
    g.setColour (col::accent);
    g.fillRoundedRectangle (progressRect_.toFloat().withWidth (progressRect_.getWidth() * prog), 1.0f);

    // --- Meters + tuner row --------------------------------------------
    {
        const int gap = 10;
        const int pw = (metersRow_.getWidth() - gap) / 2;
        juce::Rectangle<int> tuner { metersRow_.getX(), metersRow_.getY(), pw, metersRow_.getHeight() };
        juce::Rectangle<int> meters { metersRow_.getX() + pw + gap, metersRow_.getY(), pw, metersRow_.getHeight() };

        auto panel = [&] (juce::Rectangle<int> rr) {
            g.setColour (col::inkA (0.03f));
            g.fillRoundedRectangle (rr.toFloat(), 12.0f);
            g.setColour (col::inkA (0.12f));
            g.drawRoundedRectangle (rr.toFloat().reduced (0.5f), 12.0f, 1.0f);
        };

        // Tuner panel: note + bars + label
        panel (tuner);
        auto ti = tuner.reduced (14, 0);
        text ("E", displayFont (26.0f), col::ink, ti.removeFromLeft (26), juce::Justification::centred);
        text ("TUNER", uiFontTracked (10.0f, true), col::inkA (0.4f),
              ti.removeFromRight (48), juce::Justification::centred);
        {
            auto bars = ti; const int n = 5; const float bw = 3.0f;
            const float step = (float) bars.getWidth() / (float) (n + 1);
            const int heights[] = { 10, 14, 20, 14, 10 };
            for (int i = 0; i < n; ++i) {
                const float bx = bars.getX() + step * (i + 1) - bw * 0.5f;
                const float bh = (float) heights[i];
                juce::Rectangle<float> bar (bx, bars.getCentreY() - bh * 0.5f, bw, bh);
                g.setColour (i == 2 ? col::accent : col::inkA (i == 1 || i == 3 ? 0.3f : 0.2f));
                g.fillRect (bar);
            }
        }

        // Meters panel: IN + OUT rows
        panel (meters);
        auto mi = meters.reduced (14, 10);
        auto meterRow = [&] (juce::Rectangle<int> rr, const juce::String& label,
                             float level, bool peakOrange) {
            text (label, uiFontTracked (10.0f, true), col::inkA (0.4f),
                  rr.removeFromLeft (30), juce::Justification::centredLeft);
            juce::Rectangle<float> bar = rr.withSizeKeepingCentre (rr.getWidth(), 5).toFloat();
            g.setColour (col::inkA (0.10f));
            g.fillRoundedRectangle (bar, 2.5f);
            const float w = juce::jmax (5.0f, bar.getWidth() * level);
            juce::ColourGradient mg (col::meterGreen, bar.getX(), 0,
                                     peakOrange ? col::accent : col::meterLime, bar.getX() + w, 0, false);
            if (peakOrange) mg.addColour (0.7, col::meterLime);
            g.setGradientFill (mg);
            juce::Path p; p.addRoundedRectangle (bar.withWidth (w), 2.5f);
            g.fillPath (p);
        };
        meterRow (mi.removeFromTop (mi.getHeight() / 2).reduced (0, 3), "IN", inLevel_, false);
        meterRow (mi.reduced (0, 3), "OUT", outLevel_, true);
    }

    // --- Bottom nav -----------------------------------------------------
    {
        const char* labels[] = { "PLAY", "EDIT", "RADIO", "LIVE" };
        const juce::String glyphs[] = {
            juce::String::fromUTF8 ("\xE2\x96\xB6"),   // ▶
            juce::String::fromUTF8 ("\xE2\x9C\x8E"),   // ✎
            juce::String::fromUTF8 ("\xE2\x97\x89"),   // ◉
            juce::String::fromUTF8 ("\xE2\x89\xA1") }; // ≡
        for (int i = 0; i < 4; ++i) {
            const bool active = (i == 0);
            const auto c = active ? col::accent : col::inkA (0.45f);
            auto cell = navRects_[(size_t) i];
            auto icon = cell.removeFromTop (cell.getHeight() * 6 / 10);
            text (glyphs[i], uiFont (17.0f, false), c, icon.withTrimmedTop (10),
                  juce::Justification::centredBottom);
            text (labels[i], uiFontTracked (10.0f, true), c, cell,
                  juce::Justification::centredTop);
        }
    }
}

void PlayScreen::mouseDown (const juce::MouseEvent& e) {
    const auto p = e.getPosition();
    if (prevRect_.contains (p)) { index_ = (index_ - 1 + (int) tones_.size()) % (int) tones_.size(); repaint(); return; }
    if (nextRect_.contains (p)) { index_ = (index_ + 1) % (int) tones_.size(); repaint(); return; }
    if (libRect_.contains (p)) { if (onLibrary) onLibrary(); return; }
    for (int i = 0; i < 4; ++i)
        if (navRects_[(size_t) i].contains (p)) { if (onNav) onNav (i); return; }
}
