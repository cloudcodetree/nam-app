#include "app/ui/StackWidgets.h"
#include "app/ui/NamLookAndFeel.h"

namespace nam::ui {

namespace {

// Self-deleting toast body: a Timer that fires once and deletes its own
// component. No other code owns this pointer.
class ToastView : public juce::Component, private juce::Timer {
public:
    explicit ToastView (juce::String msg) : msg_ (std::move (msg)) {
        setInterceptsMouseClicks (false, false);
    }

    void start () { startTimer (2200); }

    void paint (juce::Graphics& g) override {
        const auto r = getLocalBounds ().toFloat ();
        drawPill (g, r, col::bg.withAlpha (0.96f), col::accentA (0.7f), 1.5f);
        g.setFont (uiFont (12.0f, true));
        g.setColour (col::ink);
        g.drawText (msg_, getLocalBounds ().reduced (16, 0), juce::Justification::centred, true);
    }

private:
    void timerCallback () override {
        stopTimer ();
        delete this;
    }

    juce::String msg_;
};

}   // namespace

void showToast (juce::Component& parent, juce::String msg) {
    auto* toast = new ToastView (std::move (msg));
    parent.addAndMakeVisible (toast);
    const int w = juce::jmin (320, juce::jmax (120, parent.getWidth () - 48));
    const int h = 40;
    // Bottom-anchored above the global nav bar (house overlay convention);
    // 96px clears the nav on every host that wires this in.
    toast->setBounds (parent.getWidth () / 2 - w / 2, parent.getHeight () - h - 96, w, h);
    toast->toFront (false);
    toast->start ();
}

void drawRoutingBadge (juce::Graphics& g, juce::Rectangle<int> r, nam::Stack::Routing routing) {
    const bool nonSingle = routing != nam::Stack::Routing::Single;
    const juce::String label = routing == nam::Stack::Routing::AB       ? "A/B"
                               : routing == nam::Stack::Routing::Stereo ? "STEREO"
                                                                        : "SINGLE";
    g.setColour (col::inkA (0.2f));
    g.drawRoundedRectangle (r.toFloat ().reduced (0.5f), 6.0f, 1.0f);
    g.setFont (uiFontTracked (8.0f, true));
    g.setColour (nonSingle ? col::meterLime : col::inkA (0.45f));
    g.drawText (label, r, juce::Justification::centred, false);
}

void drawFwdTriangle (juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c) {
    juce::Path p;
    p.addTriangle (b.getX (), b.getY (), b.getX (), b.getBottom (), b.getRight (), b.getCentreY ());
    g.setColour (c);
    g.fillPath (p);
}

void drawFsBadge (juce::Graphics& g, juce::Rectangle<int> r, int fs) {
    const auto b = r.toFloat ();
    const bool assigned = fs > 0;
    g.setColour (assigned ? col::accentA (0.16f) : juce::Colours::transparentBlack);
    g.fillEllipse (b);
    g.setColour (assigned ? col::accent : col::inkA (0.22f));
    g.drawEllipse (b.reduced (0.75f), 1.2f);
    g.setFont (uiFont (juce::jmin (b.getWidth (), b.getHeight ()) * 0.42f, true));
    g.setColour (assigned ? col::accent : col::inkA (0.4f));
    g.drawText (assigned ? juce::String (fs) : juce::String::fromUTF8 ("\xE2\x80\x94"),   // —
                r, juce::Justification::centred, false);
}

void drawStompCardChrome (juce::Graphics& g, juce::Rectangle<int> r, bool on) {
    const auto b = r.toFloat ();
    // Palette-only wash: accent-tinted when engaged, neutral ink when
    // bypassed. Was a per-pedal hue rotation of col::accent (seededHue) --
    // that read as a muddy, off-palette brown-olive gradient; dropped in
    // favor of the two on-brand states every other card already uses.
    const auto top = on ? col::accentA (0.20f) : col::inkA (0.07f);
    const auto bottom = on ? col::accentA (0.05f) : col::inkA (0.015f);
    juce::ColourGradient grad (top, b.getX (), b.getY (), bottom, b.getX (), b.getBottom (), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (b, 12.0f);
    g.setColour (col::inkA (0.14f));
    g.drawRoundedRectangle (b.reduced (0.5f), 12.0f, 1.0f);

    // LED, top-left: a soft glow wash behind a bright core when on.
    const auto led = juce::Rectangle<float> (b.getX () + 10.0f, b.getY () + 10.0f, 8.0f, 8.0f);
    if (on) {
        g.setColour (col::meterLime.withAlpha (0.35f));
        g.fillEllipse (led.expanded (4.0f));
    }
    g.setColour (on ? col::meterLime : col::inkA (0.2f));
    g.fillEllipse (led);

    // Three knob rings along the card's lower third, each with an indicator
    // line so they read as dials rather than placeholder circles. Angles
    // are fixed (not random/seeded) -- a deliberate "knobs turned to some
    // setting" look, not decoration masquerading as real state.
    static constexpr float kAngles[3] = { -0.9f, 0.0f, 0.7f };   // radians, 0 = pointing up
    const float ringD = juce::jmin (16.0f, b.getWidth () * 0.22f);
    const float y = b.getBottom () - ringD - 12.0f;
    const float gap = (b.getWidth () - ringD * 3.0f) / 4.0f;
    for (int i = 0; i < 3; ++i) {
        const float x = b.getX () + gap + (float)i * (ringD + gap);
        const juce::Rectangle<float> ring (x, y, ringD, ringD);
        g.setColour (on ? col::inkA (0.32f) : col::inkA (0.16f));
        g.drawEllipse (ring, 1.2f);
        const float a = kAngles[i] - juce::MathConstants<float>::halfPi;
        const float ir = ringD * 0.5f - 2.0f;
        g.setColour (on ? col::accentAlt.withAlpha (0.85f) : col::inkA (0.3f));
        g.drawLine (ring.getCentreX (), ring.getCentreY (), ring.getCentreX () + std::cos (a) * ir,
                    ring.getCentreY () + std::sin (a) * ir, 1.4f);
    }
}

void drawGrilleStrip (juce::Graphics& g, juce::Rectangle<int> r, juce::Colour c) {
    // Dark inset panel reading as an amp's recessed grille cloth: a rounded
    // rect sunk below the card's own fill, a 1px inner edge for depth, and
    // a very low-contrast horizontal line pattern (slats, not a weave) on
    // top -- only visible up close, so at card size it reads as texture
    // instead of noise. The old two-pass DIAGONAL crosshatch read as a
    // broken/missing-texture pattern rather than an intentional amp detail
    // (Chris + reviewer feedback on DONE-02-edit.png); this replaces it
    // rather than tuning its spacing/alpha again.
    const auto rf = r.toFloat ();
    g.setColour (col::bg.withAlpha (0.5f));
    g.fillRoundedRectangle (rf, 8.0f);
    g.setColour (col::inkA (0.10f));
    g.drawRoundedRectangle (rf.reduced (0.5f), 8.0f, 1.0f);

    g.saveState ();
    juce::Path clip;
    clip.addRoundedRectangle (rf, 8.0f);
    g.reduceClipRegion (clip);
    constexpr float spacing = 3.0f;
    g.setColour (c.withAlpha (c.getFloatAlpha () * 0.14f));
    for (float y = rf.getY () + spacing; y < rf.getBottom (); y += spacing)
        g.drawLine (rf.getX (), y, rf.getRight (), y, 1.0f);
    g.restoreState ();
}

void drawConePair (juce::Graphics& g, juce::Rectangle<int> r, juce::Colour c) {
    const float d = (float)juce::jmin (r.getWidth () / 2 - 2, r.getHeight ());
    const float y = r.getCentreY () - d * 0.5f;
    for (int i = 0; i < 2; ++i) {
        const float x = (float)r.getX () + (float)i * (d + 4.0f);
        g.setColour (c.withAlpha (0.7f));
        g.drawEllipse (x, y, d, d, 1.4f);
        g.setColour (c.withAlpha (0.35f));
        g.drawEllipse (x + d * 0.28f, y + d * 0.28f, d * 0.44f, d * 0.44f, 1.2f);
        g.setColour (c.withAlpha (0.6f));
        g.fillEllipse (x + d * 0.44f, y + d * 0.44f, d * 0.12f, d * 0.12f);
    }
}

}   // namespace nam::ui
