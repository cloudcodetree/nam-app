#include "app/ui/PlayScreen.h"
#include "app/ui/NamLookAndFeel.h"

#include <cmath>

using namespace nam::ui;

PlayScreen::PlayScreen() { setOpaque (true); }

void PlayScreen::setNowPlaying (juce::String name, juce::String family, juce::String author) {
    name_ = std::move (name);
    family_ = std::move (family);
    author_ = std::move (author);
    repaint();
}

void PlayScreen::setPosition (int index, int count) {
    index_ = index;
    count_ = count;
    repaint (transportRect_);
}

void PlayScreen::setTuner (juce::String note, float cents, bool active) {
    if (note == tunerNote_ && active == tunerActive_
        && std::abs (cents - tunerCents_) < 1.0f)
        return;
    tunerNote_ = std::move (note);
    tunerCents_ = cents;
    tunerActive_ = active;
    repaint (metersRow_);
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

    // Top bar: LIBRARY pill (left). I/O pill + CAB badge at right.
    libRect_ = { topBar_.getX() + 20, topBar_.getCentreY() - 16, 92, 32 };
    ioRect_  = { topBar_.getRight() - 20 - 96 - 8 - 56, topBar_.getCentreY() - 16, 56, 32 };

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

    // The tuner panel (left half of the meters row) opens the strobe tuner.
    tunerRect_ = { metersRow_.getX(), metersRow_.getY(),
                   (metersRow_.getWidth() - 10) / 2, metersRow_.getHeight() };
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

    drawPill (g, ioRect_.toFloat(), juce::Colours::transparentBlack, col::inkA (0.22f));
    text ("I/O", uiFontTracked (12.0f, true), col::ink, ioRect_, juce::Justification::centred);

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
        // big faint tone initial as "album art"
        text (name_.substring (0, 1).toUpperCase(),
              displayFont (artRect_.getHeight() * 0.62f), col::inkA (0.10f),
              artRect_, juce::Justification::centred);
        g.restoreState();
        g.setColour (col::inkA (0.10f));
        g.drawRoundedRectangle (artRect_.toFloat(), 14.0f, 1.0f);
    }

    // --- Tone text ------------------------------------------------------
    {
        auto tr = textRect_;
        text (family_.toUpperCase(), uiFontTracked (13.0f, false), col::inkA (0.45f),
              tr.removeFromTop (20), juce::Justification::topLeft);
        text (name_, displayFont (40.0f), col::ink,
              tr.removeFromTop (52), juce::Justification::topLeft);
        if (author_.isNotEmpty())
            text ("by " + author_, uiFont (13.0f, false), col::inkA (0.5f),
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
    if (count_ > 0 && index_ >= 0) {
        const float prog = (float) (index_ + 1) / (float) count_;
        g.setColour (col::accent);
        g.fillRoundedRectangle (progressRect_.toFloat().withWidth (progressRect_.getWidth() * prog), 1.0f);
    }

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

        // Tuner panel: detected note + cents-deviation bars. The centre bar
        // lights lime when in tune (within ±7 cents); off-pitch lights the
        // bar nearest the deviation in accent orange.
        panel (tuner);
        auto ti = tuner.reduced (14, 0);
        text (tunerActive_ ? tunerNote_ : juce::String::fromUTF8 ("\xE2\x80\x93"),
              displayFont (tunerActive_ && tunerNote_.length() > 2 ? 20.0f : 26.0f),
              tunerActive_ ? col::ink : col::inkA (0.35f),
              ti.removeFromLeft (40), juce::Justification::centred);
        text ("TUNER", uiFontTracked (10.0f, true), col::inkA (0.4f),
              ti.removeFromRight (48), juce::Justification::centred);
        {
            auto bars = ti; const int n = 5; const float bw = 3.0f;
            const float step = (float) bars.getWidth() / (float) (n + 1);
            const int heights[] = { 10, 14, 20, 14, 10 };
            // Which bar does the current deviation land on? (-50..50 cents)
            int lit = -1;
            bool inTune = false;
            if (tunerActive_) {
                const float c = juce::jlimit (-49.0f, 49.0f, tunerCents_);
                inTune = std::abs (c) <= 7.0f;
                lit = inTune ? 2 : juce::jlimit (0, 4, (int) std::floor ((c + 50.0f) / 20.0f));
            }
            for (int i = 0; i < n; ++i) {
                const float bx = bars.getX() + step * (i + 1) - bw * 0.5f;
                const float bh = (float) heights[i];
                juce::Rectangle<float> bar (bx, bars.getCentreY() - bh * 0.5f, bw, bh);
                if (i == lit) g.setColour (inTune ? col::meterLime : col::accent);
                else          g.setColour (col::inkA (i == 2 ? 0.35f : 0.2f));
                g.fillRect (bar);
            }
        }

        // Meters panel: IN + OUT rows, dB-scaled (-60..0 dBFS) with numeric
        // readout — the bar shows the ACTUAL level headed to the output.
        panel (meters);
        auto mi = meters.reduced (14, 10);
        auto meterRow = [&] (juce::Rectangle<int> rr, const juce::String& label,
                             float peak, bool peakOrange) {
            const float db = peak > 0.001f ? 20.0f * std::log10 (peak) : -60.0f;
            const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
            text (label, uiFontTracked (10.0f, true), col::inkA (0.4f),
                  rr.removeFromLeft (30), juce::Justification::centredLeft);
            const juce::String dbText = db <= -59.5f
                ? juce::String::fromUTF8 ("\xE2\x88\x92\xE2\x88\x9E")   // −∞
                : juce::String ((int) std::round (db));
            text (dbText, uiFont (10.0f, true),
                  db > -1.0f ? col::accent : col::inkA (0.5f),
                  rr.removeFromRight (30), juce::Justification::centredRight);
            juce::Rectangle<float> bar = rr.withSizeKeepingCentre (rr.getWidth(), 5).toFloat();
            g.setColour (col::inkA (0.10f));
            g.fillRoundedRectangle (bar, 2.5f);
            const float w = juce::jmax (frac > 0.0f ? 5.0f : 0.0f, bar.getWidth() * frac);
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
    if (prevRect_.contains (p)) { if (onPrev) onPrev(); return; }
    if (nextRect_.contains (p)) { if (onNext) onNext(); return; }
    if (libRect_.contains (p)) { if (onLibrary) onLibrary(); return; }
    if (ioRect_.contains (p))  { if (onSettings) onSettings(); return; }
    if (tunerRect_.contains (p)) { if (onTuner) onTuner(); return; }
    for (int i = 0; i < 4; ++i)
        if (navRects_[(size_t) i].contains (p)) { if (onNav) onNav (i); return; }
}
