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

void PlayScreen::setArtwork (juce::Image art) {
    art_ = std::move (art);
    repaint (artRect_);
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
    // Levels now live in the global bottom meter strip; nothing to paint here.
    inLevel_  = juce::jlimit (0.0f, 1.0f, in);
    outLevel_ = juce::jlimit (0.0f, 1.0f, out);
}

void PlayScreen::resized() { layout(); }

void PlayScreen::layout() {
    auto r = getLocalBounds();
    topBar_  = r.removeFromTop (juce::jmax (56, r.getHeight() / 14));
    metersRow_ = r.removeFromBottom (juce::jmax (78, r.getHeight() / 9)).reduced (20, 6);
    hero_    = r.reduced (26, 4);

    // Top bar: LIBRARY pill (left). CAB badge at right.
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

    // The tuner panel (full meters row) opens the strobe tuner.
    tunerRect_ = metersRow_;
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
        if (art_.isValid()) {
            // Cover-fit the TONE3000 photo (centre crop, preserve aspect).
            const float scale = juce::jmax ((float) artRect_.getWidth()  / (float) art_.getWidth(),
                                            (float) artRect_.getHeight() / (float) art_.getHeight());
            const float w = art_.getWidth() * scale, h = art_.getHeight() * scale;
            g.drawImageTransformed (art_,
                juce::AffineTransform::scale (scale)
                    .translated (artRect_.getCentreX() - w * 0.5f,
                                 artRect_.getCentreY() - h * 0.5f));
            // Bottom scrim keeps the card readable against bright photos.
            juce::ColourGradient scrim (col::bg.withAlpha (0.55f), (float) artRect_.getCentreX(),
                                        (float) artRect_.getBottom(), col::bg.withAlpha (0.0f),
                                        (float) artRect_.getCentreX(),
                                        (float) artRect_.getCentreY(), false);
            g.setGradientFill (scrim); g.fillRect (artRect_);
        } else {
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
        }
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

    // --- Tuner row (levels live in the global bottom meter) -------------
    {
        juce::Rectangle<int> tuner = metersRow_;

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
        auto ti = tuner.reduced (18, 0);
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

    }
}

void PlayScreen::mouseUp (const juce::MouseEvent& e) {
    // Swipe across the hero area steps through the collection.
    if (! hero_.contains (pressPos_)) return;
    const int dx = e.getPosition().x - pressPos_.x;
    const int dy = e.getPosition().y - pressPos_.y;
    if (std::abs (dx) > 60 && std::abs (dx) > std::abs (dy) * 2) {
        if (dx < 0) { if (onNext) onNext(); }
        else        { if (onPrev) onPrev(); }
    }
}

void PlayScreen::mouseDown (const juce::MouseEvent& e) {
    const auto p = e.getPosition();
    pressPos_ = p;
    if (prevRect_.contains (p)) { if (onPrev) onPrev(); return; }
    if (nextRect_.contains (p)) { if (onNext) onNext(); return; }
    if (libRect_.contains (p)) { if (onLibrary) onLibrary(); return; }
    if (tunerRect_.contains (p)) { if (onTuner) onTuner(); return; }
}
