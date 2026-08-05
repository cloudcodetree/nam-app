#include "app/ui/SetupScreen.h"
#include "app/ui/NamLookAndFeel.h"

#include <cmath>

using namespace nam::ui;

namespace {
const juce::String kDot      = juce::String::fromUTF8 ("\xC2\xB7");     // ·
const juce::String kEllipsis = juce::String::fromUTF8 ("\xE2\x80\xA6"); // …
const juce::String kCheck    = juce::String::fromUTF8 ("\xE2\x9C\x93"); // ✓
}

SetupScreen::SetupScreen() { setOpaque (true); startTimerHz (30); }
SetupScreen::~SetupScreen() { stopTimer(); }

void SetupScreen::setLevel (float inPeak) {
    level_ = juce::jlimit (0.0f, 1.0f, inPeak);
    if (listening_) maxPeak_ = juce::jmax (maxPeak_, inPeak);
}

void SetupScreen::finishListening() {
    if (! listening_) return;
    listening_ = false;
    // Aim the loudest observed peak at ~-12 dBFS of headroom.
    const float target = 0.25f;
    if (maxPeak_ > 0.02f)
        appliedDb_ = juce::jlimit (-24.0f, 24.0f, 20.0f * std::log10 (target / maxPeak_));
    if (onSetInputDb) onSetInputDb (appliedDb_);
    repaint();
}

void SetupScreen::timerCallback() {
    ++ticks_;
    // Auto-advance once we've heard a solid signal for a moment, or after ~6s.
    if (listening_ && ((maxPeak_ > 0.15f && ticks_ > 45) || ticks_ > 180))
        finishListening();
    repaint();
}

void SetupScreen::resized() {
    auto r = getLocalBounds().reduced (28);
    auto btns = r.removeFromBottom (120);
    primaryBtn_   = btns.removeFromTop (56);
    btns.removeFromTop (10);
    secondaryBtn_ = btns.removeFromTop (48);
}

void SetupScreen::paint (juce::Graphics& g) {
    paintHeroBackground (g, getLocalBounds());
    auto text = [&] (const juce::String& s, juce::Font f, juce::Colour c,
                     juce::Rectangle<int> rr, juce::Justification j) {
        g.setFont (f); g.setColour (c); g.drawText (s, rr, j, false);
    };

    text ("NAM PLAYER", uiFontTracked (10.0f, true), col::accentAlt, { 28, 34, 260, 16 },
          juce::Justification::topLeft);
    text (listening_ ? "Guitar detected." : "You're set.", displayFont (34.0f), col::ink,
          { 28, 50, getWidth() - 56, 48 }, juce::Justification::topLeft);

    auto centre = getLocalBounds().withTrimmedTop (150).withTrimmedBottom (150);

    if (listening_) {
        // Animated listening bars
        const int n = 5;
        const float bw = 6.0f, gap = 10.0f;
        const float totalW = n * bw + (n - 1) * gap;
        float x = centre.getCentreX() - totalW * 0.5f;
        const float cy = (float) centre.getCentreY() - 20.0f;
        for (int i = 0; i < n; ++i) {
            const float ph = (float) ticks_ * 0.18f + (float) i * 0.7f;
            const float h = 20.0f + (0.5f + 0.5f * std::sin (ph)) * (30.0f + level_ * 80.0f);
            g.setColour (col::accent.withAlpha (0.85f));
            g.fillRoundedRectangle (x, cy - h * 0.5f, bw, h, bw * 0.5f);
            x += bw + gap;
        }
        text ("Play a few open chords" + kEllipsis, uiFont (15.0f, true), col::ink,
              centre.withTrimmedTop (centre.getHeight() / 2 + 10).removeFromTop (24),
              juce::Justification::centred);
        text ("setting your input level automatically", uiFont (13.0f, false), col::inkA (0.5f),
              centre.withTrimmedTop (centre.getHeight() / 2 + 36).removeFromTop (20),
              juce::Justification::centred);
    } else {
        auto badge = juce::Rectangle<int> (centre.getCentreX() - 38, centre.getY() + 20, 76, 76);
        drawPill (g, badge.toFloat(), col::accentA (0.08f), col::accentA (0.5f));
        text (kCheck, uiFont (30.0f, false), col::accentAlt, badge,
              juce::Justification::centred);
        text ("Input set " + kDot + " " + juce::String (appliedDb_, 1) + " dB", uiFont (15.0f, true), col::ink,
              centre.withTrimmedTop (110).removeFromTop (24), juce::Justification::centred);
        text ("48 kHz " + kDot + " low-latency", uiFont (13.0f, false), col::inkA (0.5f),
              centre.withTrimmedTop (134).removeFromTop (20), juce::Justification::centred);
    }

    // Level bar
    auto bar = juce::Rectangle<int> (28, centre.getBottom() + 6, getWidth() - 56, 4);
    g.setColour (col::inkA (0.1f));
    g.fillRoundedRectangle (bar.toFloat(), 2.0f);
    const float w = juce::jmax (2.0f, bar.getWidth() * (listening_ ? level_ : juce::jmin (1.0f, maxPeak_)));
    juce::ColourGradient mg (col::meterGreen, (float) bar.getX(), 0, col::meterLime,
                             (float) bar.getX() + w, 0, false);
    g.setGradientFill (mg);
    g.fillRoundedRectangle (bar.toFloat().withWidth (w), 2.0f);

    // Buttons
    if (listening_) {
        drawPill (g, primaryBtn_.toFloat(), col::inkA (0.0f), col::inkA (0.22f));
        g.setColour (col::inkA (0.7f)); g.setFont (uiFontTracked (13.0f, true));
        g.drawText ("SET LEVEL MANUALLY", primaryBtn_, juce::Justification::centred, false);
    } else {
        g.setColour (col::accent);
        g.fillRoundedRectangle (primaryBtn_.toFloat(), 16.0f);
        g.setColour (col::inkOnAccent); g.setFont (uiFontTracked (14.0f, true));
        g.drawText ("CONTINUE", primaryBtn_, juce::Justification::centred, false);
        g.setColour (col::inkA (0.5f)); g.setFont (uiFont (13.0f, false));
        g.drawText ("skip " + kDot + " use bundled tones", secondaryBtn_, juce::Justification::centred, false);
    }
}

void SetupScreen::mouseDown (const juce::MouseEvent& e) {
    const auto p = e.getPosition();
    if (primaryBtn_.contains (p)) {
        if (listening_) finishListening();
        else if (onFinish) onFinish();
        return;
    }
    if (! listening_ && secondaryBtn_.contains (p)) { if (onFinish) onFinish(); return; }
}
