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

    gearRect_ = { artRect_.getRight() - 54, artRect_.getY() + 14, 40, 40 };

    // Card-back settings rows (drawn/hit only while flipped).
    {
        auto inner = artRect_.reduced (24, 18);
        inner.removeFromTop (30);   // title strip
        const int rowH = inner.getHeight() / kNumToneParams;
        for (int i = 0; i < kNumToneParams; ++i)
            paramRows_[(size_t) i] = inner.removeFromTop (rowH).reduced (0, 6);
    }
}

void PlayScreen::toggleFlip() {
    flipped_ = ! flipped_;
    startTimerHz (60);
}

void PlayScreen::timerCallback() {
    const float target = flipped_ ? 1.0f : 0.0f;
    const float step = 1.0f / 11.0f;
    flip_ += (target > flip_ ? step : -step);
    flip_ = juce::jlimit (0.0f, 1.0f, flip_);
    if (std::abs (flip_ - target) < 0.001f) { flip_ = target; stopTimer(); }
    repaint (artRect_.expanded (12));
}

void PlayScreen::applyParamFromX (int idx, int x) {
    auto track = paramRows_[(size_t) idx].withTrimmedLeft (86);
    const float v = juce::jlimit (0.0f, 1.0f,
        (float) (x - track.getX()) / (float) juce::jmax (1, track.getWidth()));
    params_[(size_t) idx].v = v;
    if (onToneParam) onToneParam (idx, v);
    repaint (paramRows_[(size_t) idx].expanded (8));
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

    // --- Hero card: artwork front / settings back (flip = squash-X) -----
    {
        const float sc = std::abs (std::cos (flip_ * juce::MathConstants<float>::pi));
        const auto face = artRect_.withSizeKeepingCentre (
            juce::jmax (2, (int) ((float) artRect_.getWidth() * sc)), artRect_.getHeight());
        const bool backFace = flip_ >= 0.5f;

        juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 34,
                          { 0, 18 }).drawForRectangle (g, face);
        juce::Path clip; clip.addRoundedRectangle (face.toFloat(), 14.0f);
        g.saveState();
        g.reduceClipRegion (clip);
        if (backFace) {
            // Settings face: panel wash + quick per-tone sliders.
            juce::ColourGradient bg (col::bgGradTop.brighter (0.08f), (float) face.getCentreX(),
                                     (float) face.getY(), col::bg, (float) face.getCentreX(),
                                     (float) face.getBottom(), false);
            g.setGradientFill (bg); g.fillRect (face);
            if (flip_ > 0.92f) {
                text ("TONE SETTINGS", uiFontTracked (11.0f, true), col::inkA (0.45f),
                      { artRect_.getX() + 24, artRect_.getY() + 16, artRect_.getWidth() - 48, 20 },
                      juce::Justification::centredLeft);
                for (int i = 0; i < kNumToneParams; ++i) {
                    const auto row = paramRows_[(size_t) i];
                    const auto& pm = params_[(size_t) i];
                    text (pm.label, uiFontTracked (10.0f, true), col::inkA (0.6f),
                          row.withWidth (80), juce::Justification::centredLeft);
                    auto track = row.withTrimmedLeft (86);
                    const float cy = (float) track.getCentreY();
                    g.setColour (col::inkA (0.12f));
                    g.fillRoundedRectangle ((float) track.getX(), cy - 2.0f,
                                            (float) track.getWidth(), 4.0f, 2.0f);
                    const float fx = (float) track.getX() + (float) track.getWidth() * pm.v;
                    g.setColour (col::accentA (0.85f));
                    g.fillRoundedRectangle ((float) track.getX(), cy - 2.0f,
                                            juce::jmax (4.0f, fx - (float) track.getX()), 4.0f, 2.0f);
                    g.setColour (col::ink);
                    g.fillEllipse (fx - 7.0f, cy - 7.0f, 14.0f, 14.0f);
                }
            }
        } else if (art_.isValid()) {
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
        g.setColour (col::inkA (backFace ? 0.14f : 0.10f));
        g.drawRoundedRectangle (face.toFloat(), 14.0f, 1.0f);

        // Corner affordance at rest: gear opens settings, ‹ goes back.
        if (flip_ < 0.05f || flip_ > 0.95f) {
            g.setColour (col::bg.withAlpha (0.5f));
            g.fillEllipse (gearRect_.toFloat());
            g.setColour (col::inkA (0.3f));
            g.drawEllipse (gearRect_.toFloat().reduced (0.5f), 1.0f);
            text (juce::String::fromUTF8 (backFace ? "\xE2\x80\xB9" : "\xE2\x9A\x99"),
                  uiFont (backFace ? 22.0f : 18.0f, false), col::ink,
                  backFace ? gearRect_.translated (-1, -2) : gearRect_,
                  juce::Justification::centred);
        }
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
            // Pedal-style LED segments (same look as the expanded BARS mode):
            // 11 segments over -50..+50 cents, centre = in tune.
            auto bars = ti.reduced (6, 0);
            const int n = 11, gap = 5;
            const int segW = juce::jmax (3, (bars.getWidth() - (n - 1) * gap) / n);
            const int mid = n / 2;
            int lit = -1;
            bool inTune = false;
            if (tunerActive_) {
                const float c = juce::jlimit (-49.0f, 49.0f, tunerCents_);
                inTune = std::abs (c) <= 7.0f;
                lit = inTune ? mid
                             : juce::jlimit (0, n - 1,
                                   (int) std::floor ((c + 50.0f) / (100.0f / (float) n)));
            }
            const int baseH = 14;
            for (int i = 0; i < n; ++i) {
                const int grow = (i == mid) ? baseH / 2 : baseH / 5 * std::abs (i - mid) / mid;
                const int h = baseH + (i == mid ? baseH / 2 : grow);
                juce::Rectangle<int> seg (bars.getX() + i * (segW + gap),
                                          bars.getCentreY() - h / 2, segW, h);
                const bool on = (i == lit);
                const juce::Colour c = ! on ? col::inkA (0.10f)
                                     : i == mid ? col::meterLime
                                                : col::accent.withAlpha (0.9f);
                if (on) {   // glow
                    g.setColour (c.withAlpha (0.25f));
                    g.fillRoundedRectangle (seg.toFloat().expanded (4.0f), 7.0f);
                }
                g.setColour (c);
                g.fillRoundedRectangle (seg.toFloat(), 3.0f);
            }
        }

    }
}

void PlayScreen::mouseUp (const juce::MouseEvent& e) {
    if (dragParam_ >= 0) { dragParam_ = -1; return; }
    const int dx = e.getPosition().x - pressPos_.x;
    const int dy = e.getPosition().y - pressPos_.y;
    const bool tap = std::abs (dx) < 12 && std::abs (dy) < 12;

    // Settings face: any tap that isn't an interactive element flips back
    // (sliders end up in dragParam_; buttons acted in mouseDown). Swipes
    // that START outside the card still step tones — the new tone keeps
    // showing whichever face is up (flip state is untouched).
    if (flipped_) {
        if (tap && ! prevRect_.contains (pressPos_) && ! nextRect_.contains (pressPos_)
                && ! libRect_.contains (pressPos_) && ! tunerRect_.contains (pressPos_)) {
            toggleFlip();
            return;
        }
        if (! artRect_.contains (pressPos_) && hero_.contains (pressPos_)
            && std::abs (dx) > 60 && std::abs (dx) > std::abs (dy) * 2) {
            if (dx < 0) { if (onNext) onNext(); }
            else        { if (onPrev) onPrev(); }
        }
        return;
    }

    // A tap on the card flips it around to the settings face.
    if (tap && artRect_.contains (pressPos_)) { toggleFlip(); return; }

    // Swipe across the hero area steps through the collection (front only).
    if (! hero_.contains (pressPos_)) return;
    if (std::abs (dx) > 60 && std::abs (dx) > std::abs (dy) * 2) {
        if (dx < 0) { if (onNext) onNext(); }
        else        { if (onPrev) onPrev(); }
    }
}

void PlayScreen::mouseDrag (const juce::MouseEvent& e) {
    if (dragParam_ >= 0) applyParamFromX (dragParam_, e.getPosition().x);
}

void PlayScreen::mouseDown (const juce::MouseEvent& e) {
    const auto p = e.getPosition();
    pressPos_ = p;
    // Settings face: grab a slider row.
    if (flipped_ && flip_ >= 1.0f)
        for (int i = 0; i < kNumToneParams; ++i)
            if (paramRows_[(size_t) i].expanded (0, 6).contains (p)) {
                dragParam_ = i;
                applyParamFromX (i, p.x);
                return;
            }
    if (prevRect_.contains (p)) { if (onPrev) onPrev(); return; }
    if (nextRect_.contains (p)) { if (onNext) onNext(); return; }
    if (libRect_.contains (p)) { if (onLibrary) onLibrary(); return; }
    if (tunerRect_.contains (p)) { if (onTuner) onTuner(); return; }
}
