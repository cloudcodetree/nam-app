#include "app/ui/TunerScreen.h"
#include "app/ui/NamLookAndFeel.h"

#include <cmath>

using namespace nam::ui;

namespace {
const juce::String kBackGlyph = juce::String::fromUTF8 ("\xE2\x80\xB9");   // ‹
const juce::String kDash = juce::String::fromUTF8 ("\xE2\x80\x93");        // –
const juce::String kCentsSign = juce::String::fromUTF8 ("\xC2\xA2");       // ¢
}   // namespace

TunerScreen::TunerScreen () {
    setOpaque (true);
    startTimerHz (60);
}
TunerScreen::~TunerScreen () { stopTimer (); }

void TunerScreen::setPanelMode (bool on) {
    panelMode_ = on;
    setOpaque (!on);   // rounded card corners are transparent
    if (!getLocalBounds ().isEmpty ()) resized ();
    repaint ();
}

void TunerScreen::setPitch (float hz) {
    if (hz <= 0.0f) {
        // Hold the last reading briefly so the display doesn't flicker
        // between plucks; timerCallback fades it out.
        return;
    }
    hz_ = hz;
    inactiveTicks_ = 0;
    static const char* names[] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    const double midi = 69.0 + 12.0 * std::log2 ((double)hz / 440.0);
    const int nearest = (int)std::round (midi);
    const float target = (float)((midi - (double)nearest) * 100.0);
    const int nameIdx = ((nearest % 12) + 12) % 12;
    note_ = juce::String (names[nameIdx]) + juce::String (nearest / 12 - 1);
    // Smooth the cents for a stable strobe; snap when far off.
    cents_ = std::abs (target - cents_) > 25.0f ? target : cents_ + 0.35f * (target - cents_);
    active_ = true;
}

void TunerScreen::timerCallback () {
    if (!isVisible ()) return;
    if (++inactiveTicks_ > 75 && active_) {   // ~1.25 s without pitch
        active_ = false;
        note_.clear ();
    }
    if (active_) {
        // Drift speed proportional to deviation (px/frame at 60 fps).
        phase_ += (double)cents_ * 0.10;
        repaint (
            bandsRect_.expanded (12).getUnion (noteRect_).getUnion (centsRect_).getUnion (hzRect_));
    } else if (inactiveTicks_ == 46) {
        repaint ();
    }
}

void TunerScreen::resized () {
    auto r = getLocalBounds ();
    if (panelMode_) {
        backRect_ = {};   // the panel below is the collapse handle
        r.reduce (20, 14);
    } else {
        auto top = r.removeFromTop (juce::jmax (56, r.getHeight () / 14));
        backRect_ = { top.getX () + 20, top.getCentreY () - 16, 84, 32 };
        r.reduce (26, 0);
    }
    modeRow_ = r.removeFromTop (40);
    {
        const int w = juce::jmin (110, (modeRow_.getWidth () - 16) / 3);
        const int total = w * 3 + 16;
        int x = modeRow_.getCentreX () - total / 2;
        for (auto& mr : modeRects_) {
            mr = { x, modeRow_.getY () + 4, w, 32 };
            x += w + 8;
        }
    }
    noteRect_ = r.removeFromTop (r.getHeight () / 3).withTrimmedTop (8);
    centsRect_ = r.removeFromTop (44);
    hzRect_ = r.removeFromTop (26);
    r.removeFromTop (14);
    bandsRect_ = r.withTrimmedBottom (r.getHeight () / 4);
}

void TunerScreen::paint (juce::Graphics& g) {
    if (panelMode_) {
        // Same card treatment as the Play tuner panel it grows out of.
        const auto card = getLocalBounds ().toFloat ();
        g.setColour (col::bg);
        g.fillRoundedRectangle (card, 12.0f);
        g.setColour (col::inkA (0.03f));
        g.fillRoundedRectangle (card, 12.0f);
        g.setColour (col::inkA (0.12f));
        g.drawRoundedRectangle (card.reduced (0.5f), 12.0f, 1.0f);
    } else {
        paintHeroBackground (g, getLocalBounds ());
    }
    auto text = [&] (const juce::String& s, juce::Font f, juce::Colour c, juce::Rectangle<int> rr,
                     juce::Justification j) {
        g.setFont (f);
        g.setColour (c);
        g.drawText (s, rr, j, false);
    };

    if (!panelMode_) {
        drawPill (g, backRect_.toFloat (), juce::Colours::transparentBlack, col::inkA (0.22f));
        text (kBackGlyph + " BACK", uiFontTracked (12.0f, true), col::ink, backRect_,
              juce::Justification::centred);
    }

    const bool inTune = active_ && std::abs (cents_) <= 3.0f;

    // Note + readouts
    text (active_ ? note_ : kDash, displayFont ((float)noteRect_.getHeight () * 0.72f),
          inTune    ? col::meterLime
          : active_ ? col::ink
                    : col::inkA (0.25f),
          noteRect_, juce::Justification::centred);
    if (active_) {
        const int c = (int)std::round (cents_);
        text ((c > 0 ? "+" : "") + juce::String (c) + kCentsSign, uiFont (24.0f, true),
              inTune ? col::meterLime : col::accentAlt, centsRect_, juce::Justification::centred);
        text (juce::String (hz_, 1) + " Hz", uiFont (13.0f, false), col::inkA (0.45f), hzRect_,
              juce::Justification::centred);
    } else {
        text ("play a single string", uiFont (14.0f, false), col::inkA (0.4f), centsRect_,
              juce::Justification::centred);
    }

    // Mode pills
    static const char* kModeNames[] = { "STROBE", "NEEDLE", "BARS" };
    for (int m = 0; m < 3; ++m) {
        const bool on = ((int)mode_ == m);
        drawPill (g, modeRects_[(size_t)m].toFloat (),
                  on ? col::accentA (0.12f) : juce::Colours::transparentBlack,
                  on ? col::accentA (0.6f) : col::inkA (0.18f));
        text (kModeNames[m], uiFontTracked (10.0f, true), on ? col::accentAlt : col::inkA (0.6f),
              modeRects_[(size_t)m], juce::Justification::centred);
    }

    switch (mode_) {
        case Mode::Strobe: paintStrobe (g, inTune); break;
        case Mode::Needle: paintNeedle (g, inTune); break;
        case Mode::Bars: paintBars (g, inTune); break;
    }
}

void TunerScreen::paintStrobe (juce::Graphics& g, bool inTune) {
    // Strobe bands: 1x / 2x / 4x. Alternating segments scrolled by phase;
    // apparent motion stops when in tune.
    const int nBands = 3;
    const int gap = 14;
    const int bandH = (bandsRect_.getHeight () - (nBands - 1) * gap) / nBands;
    for (int b = 0; b < nBands; ++b) {
        juce::Rectangle<int> band (bandsRect_.getX (), bandsRect_.getY () + b * (bandH + gap),
                                   bandsRect_.getWidth (), bandH);
        g.setColour (col::inkA (0.05f));
        g.fillRoundedRectangle (band.toFloat (), 8.0f);

        const float seg = 44.0f / (float)(1 << b);   // halve segment width per band
        const float speedMul = (float)(1 << b);
        const float offset = (float)std::fmod (phase_ * speedMul, (double)(seg * 2.0f));

        g.saveState ();
        juce::Path clip;
        clip.addRoundedRectangle (band.toFloat (), 8.0f);
        g.reduceClipRegion (clip);
        const auto segColour = !active_ ? col::inkA (0.12f)
                               : inTune ? col::meterLime.withAlpha (0.75f)
                                        : col::accentA (0.6f);
        g.setColour (segColour);
        for (float x = band.getX () - seg * 2.0f + offset; x < (float)band.getRight ();
             x += seg * 2.0f)
            g.fillRect (x, (float)band.getY (), seg, (float)band.getHeight ());
        g.restoreState ();

        g.setColour (col::inkA (0.10f));
        g.drawRoundedRectangle (band.toFloat ().reduced (0.5f), 8.0f, 1.0f);
    }

    // Centre reference line across the bands
    g.setColour (col::inkA (0.35f));
    g.fillRect (bandsRect_.getCentreX () - 1, bandsRect_.getY () - 6, 2,
                bandsRect_.getHeight () + 12);
}

void TunerScreen::paintNeedle (juce::Graphics& g, bool inTune) {
    auto text = [&] (const juce::String& s, juce::Font f, juce::Colour c, juce::Rectangle<int> rr,
                     juce::Justification j) {
        g.setFont (f);
        g.setColour (c);
        g.drawText (s, rr, j, false);
    };

    // Dial geometry: pivot at bottom-centre, needle sweeps ±45°.
    const float cx = (float)bandsRect_.getCentreX ();
    const float cy = (float)bandsRect_.getBottom () - 12.0f;
    const float radius =
        juce::jmin ((float)bandsRect_.getWidth () * 0.46f, (float)bandsRect_.getHeight () - 30.0f);
    const float maxAngle = juce::MathConstants<float>::pi * 0.25f;

    // Scale arc + ticks every 10 cents (bigger at 0 and ±50).
    for (int c = -50; c <= 50; c += 10) {
        const float a = ((float)c / 50.0f) * maxAngle;
        const float sinA = std::sin (a), cosA = std::cos (a);
        const float r1 = radius * (c == 0 ? 0.82f : 0.88f);
        const float r2 = radius * 0.98f;
        g.setColour (c == 0 ? col::inkA (0.6f) : col::inkA (0.25f));
        g.drawLine (cx + sinA * r1, cy - cosA * r1, cx + sinA * r2, cy - cosA * r2,
                    c == 0 ? 2.5f : 1.5f);
    }
    text (juce::String::fromUTF8 ("\xE2\x99\xAD"), uiFont (18.0f, false), col::inkA (0.4f),
          { bandsRect_.getX () + 8, bandsRect_.getY () + 8, 30, 30 }, juce::Justification::centred);
    text (juce::String::fromUTF8 ("\xE2\x99\xAF"), uiFont (18.0f, false), col::inkA (0.4f),
          { bandsRect_.getRight () - 38, bandsRect_.getY () + 8, 30, 30 },
          juce::Justification::centred);

    // Needle
    const float clamped = juce::jlimit (-50.0f, 50.0f, active_ ? cents_ : 0.0f);
    const float na = (clamped / 50.0f) * maxAngle;
    const float nx = cx + std::sin (na) * radius * 0.92f;
    const float ny = cy - std::cos (na) * radius * 0.92f;
    g.setColour (!active_ ? col::inkA (0.2f) : inTune ? col::meterLime : col::accent);
    g.drawLine (cx, cy, nx, ny, 3.0f);
    g.fillEllipse (cx - 7.0f, cy - 7.0f, 14.0f, 14.0f);
}

void TunerScreen::paintBars (juce::Graphics& g, bool inTune) {
    // Pedal-style LEDs: 11 segments over -50..+50 cents, centre = in tune.
    const int n = 11;
    const int gap = 8;
    const int segW = (bandsRect_.getWidth () - (n - 1) * gap) / n;
    const int mid = n / 2;
    int lit = -1;
    if (active_)
        lit = inTune
                  ? mid
                  : juce::jlimit (0, n - 1,
                                  (int)std::floor ((juce::jlimit (-49.0f, 49.0f, cents_) + 50.0f) /
                                                   (100.0f / (float)n)));
    const int baseH = bandsRect_.getHeight () / 3;
    for (int i = 0; i < n; ++i) {
        const int grow = (i == mid) ? baseH / 2 : baseH / 5 * std::abs (i - mid) / mid;
        const int h = baseH + (i == mid ? baseH / 2 : grow);
        juce::Rectangle<int> seg (bandsRect_.getX () + i * (segW + gap),
                                  bandsRect_.getCentreY () - h / 2, segW, h);
        const bool on = (i == lit);
        juce::Colour c;
        if (!on) c = col::inkA (0.10f);
        else if (i == mid) c = col::meterLime;
        else c = col::accent.withAlpha (0.9f);
        g.setColour (c);
        g.fillRoundedRectangle (seg.toFloat (), 6.0f);
        if (on) {   // glow
            g.setColour (c.withAlpha (0.25f));
            g.fillRoundedRectangle (seg.toFloat ().expanded (5.0f), 9.0f);
        }
    }
}

void TunerScreen::mouseDown (const juce::MouseEvent& e) {
    const auto p = e.getPosition ();
    if (backRect_.expanded (10).contains (p)) {
        if (onBack) onBack ();
        return;
    }
    for (int m = 0; m < 3; ++m)
        if (modeRects_[(size_t)m].contains (p)) {
            mode_ = (Mode)m;
            repaint ();
            return;
        }
    // Panel mode: any tap that isn't a mode pill dismisses the card (the
    // outside scrim handles taps beyond it).
    if (panelMode_ && onBack) onBack ();
}
