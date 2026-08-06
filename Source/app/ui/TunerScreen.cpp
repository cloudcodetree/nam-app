#include "app/ui/TunerScreen.h"
#include "app/ui/NamLookAndFeel.h"

#include <cmath>

using namespace nam::ui;

namespace {
const juce::String kBackGlyph = juce::String::fromUTF8 ("\xE2\x80\xB9"); // ‹
const juce::String kDash      = juce::String::fromUTF8 ("\xE2\x80\x93"); // –
const juce::String kCentsSign = juce::String::fromUTF8 ("\xC2\xA2");     // ¢
}

TunerScreen::TunerScreen() { setOpaque (true); startTimerHz (60); }
TunerScreen::~TunerScreen() { stopTimer(); }

void TunerScreen::setPitch (float hz) {
    if (hz <= 0.0f) {
        // Hold the last reading briefly so the display doesn't flicker
        // between plucks; timerCallback fades it out.
        return;
    }
    hz_ = hz;
    inactiveTicks_ = 0;
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F",
                                   "F#", "G", "G#", "A", "A#", "B" };
    const double midi = 69.0 + 12.0 * std::log2 ((double) hz / 440.0);
    const int nearest = (int) std::round (midi);
    const float target = (float) ((midi - (double) nearest) * 100.0);
    const int nameIdx = ((nearest % 12) + 12) % 12;
    note_ = juce::String (names[nameIdx]) + juce::String (nearest / 12 - 1);
    // Smooth the cents for a stable strobe; snap when far off.
    cents_ = std::abs (target - cents_) > 25.0f ? target
                                                : cents_ + 0.35f * (target - cents_);
    active_ = true;
}

void TunerScreen::timerCallback() {
    if (! isVisible()) return;
    if (++inactiveTicks_ > 45 && active_) {   // ~0.75 s without pitch
        active_ = false;
        note_.clear();
    }
    if (active_) {
        // Drift speed proportional to deviation (px/frame at 60 fps).
        phase_ += (double) cents_ * 0.10;
        repaint (bandsRect_.getUnion (noteRect_).getUnion (centsRect_).getUnion (hzRect_));
    } else if (inactiveTicks_ == 46) {
        repaint();
    }
}

void TunerScreen::resized() {
    auto r = getLocalBounds();
    auto top = r.removeFromTop (juce::jmax (56, r.getHeight() / 14));
    backRect_ = { top.getX() + 20, top.getCentreY() - 16, 84, 32 };

    r.reduce (26, 0);
    noteRect_  = r.removeFromTop (r.getHeight() / 3).withTrimmedTop (20);
    centsRect_ = r.removeFromTop (44);
    hzRect_    = r.removeFromTop (26);
    r.removeFromTop (18);
    bandsRect_ = r.withTrimmedBottom (r.getHeight() / 4);
}

void TunerScreen::paint (juce::Graphics& g) {
    paintHeroBackground (g, getLocalBounds());
    auto text = [&] (const juce::String& s, juce::Font f, juce::Colour c,
                     juce::Rectangle<int> rr, juce::Justification j) {
        g.setFont (f); g.setColour (c); g.drawText (s, rr, j, false);
    };

    drawPill (g, backRect_.toFloat(), juce::Colours::transparentBlack, col::inkA (0.22f));
    text (kBackGlyph + " BACK", uiFontTracked (12.0f, true), col::ink,
          backRect_, juce::Justification::centred);

    const bool inTune = active_ && std::abs (cents_) <= 3.0f;

    // Note + readouts
    text (active_ ? note_ : kDash, displayFont ((float) noteRect_.getHeight() * 0.72f),
          inTune ? col::meterLime : active_ ? col::ink : col::inkA (0.25f),
          noteRect_, juce::Justification::centred);
    if (active_) {
        const int c = (int) std::round (cents_);
        text ((c > 0 ? "+" : "") + juce::String (c) + kCentsSign,
              uiFont (24.0f, true), inTune ? col::meterLime : col::accentAlt,
              centsRect_, juce::Justification::centred);
        text (juce::String (hz_, 1) + " Hz", uiFont (13.0f, false), col::inkA (0.45f),
              hzRect_, juce::Justification::centred);
    } else {
        text ("play a single string", uiFont (14.0f, false), col::inkA (0.4f),
              centsRect_, juce::Justification::centred);
    }

    // Strobe bands: 1x / 2x / 4x. Alternating segments scrolled by phase;
    // apparent motion stops when in tune.
    const int nBands = 3;
    const int gap = 14;
    const int bandH = (bandsRect_.getHeight() - (nBands - 1) * gap) / nBands;
    for (int b = 0; b < nBands; ++b) {
        juce::Rectangle<int> band (bandsRect_.getX(),
                                   bandsRect_.getY() + b * (bandH + gap),
                                   bandsRect_.getWidth(), bandH);
        g.setColour (col::inkA (0.05f));
        g.fillRoundedRectangle (band.toFloat(), 8.0f);

        const float seg = 44.0f / (float) (1 << b);   // halve segment width per band
        const float speedMul = (float) (1 << b);
        const float offset = (float) std::fmod (phase_ * speedMul, (double) (seg * 2.0f));

        g.saveState();
        juce::Path clip;
        clip.addRoundedRectangle (band.toFloat(), 8.0f);
        g.reduceClipRegion (clip);
        const auto segColour = ! active_ ? col::inkA (0.12f)
                               : inTune ? col::meterLime.withAlpha (0.75f)
                                        : col::accentA (0.6f);
        g.setColour (segColour);
        for (float x = band.getX() - seg * 2.0f + offset; x < (float) band.getRight(); x += seg * 2.0f)
            g.fillRect (x, (float) band.getY(), seg, (float) band.getHeight());
        g.restoreState();

        g.setColour (col::inkA (0.10f));
        g.drawRoundedRectangle (band.toFloat().reduced (0.5f), 8.0f, 1.0f);
    }

    // Centre reference line across the bands
    g.setColour (col::inkA (0.35f));
    g.fillRect (bandsRect_.getCentreX() - 1, bandsRect_.getY() - 6, 2,
                bandsRect_.getHeight() + 12);
}

void TunerScreen::mouseDown (const juce::MouseEvent& e) {
    if (backRect_.expanded (10).contains (e.getPosition())) {
        if (onBack) onBack();
    }
}
