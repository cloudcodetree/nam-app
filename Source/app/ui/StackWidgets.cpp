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

void drawSetlistChip (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text,
                      bool active) {
    drawPill (g, r.toFloat (), active ? col::accentA (0.16f) : juce::Colours::transparentBlack,
              active ? col::accent : col::inkA (0.16f));
    g.setFont (uiFont (11.0f, active));
    g.setColour (active ? col::accent : col::inkA (0.6f));
    g.drawText (text, r.reduced (10, 0), juce::Justification::centred, false);
}

void drawGearIcon (juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c) {
    const float cx = b.getCentreX (), cy = b.getCentreY ();
    const float rOuter = b.getWidth () * 0.30f, rInner = b.getWidth () * 0.13f;
    g.setColour (c);
    constexpr int kTeeth = 8;
    for (int i = 0; i < kTeeth; ++i) {
        const float a = (float)i / (float)kTeeth * juce::MathConstants<float>::twoPi;
        juce::Path tooth;
        tooth.addRoundedRectangle (-1.6f, -rOuter - 3.0f, 3.2f, 6.0f, 1.0f);
        tooth.applyTransform (juce::AffineTransform::rotation (a).translated (cx, cy));
        g.fillPath (tooth);
    }
    g.drawEllipse (cx - rOuter, cy - rOuter, rOuter * 2.0f, rOuter * 2.0f, 2.0f);
    g.fillEllipse (cx - rInner, cy - rInner, rInner * 2.0f, rInner * 2.0f);
}

void drawStacksBrandHeader (juce::Graphics& g, juce::Rectangle<int> bounds,
                            juce::Rectangle<int> gearRect) {
    g.setFont (uiFontTracked (11.0f, true));
    auto nameRect = bounds.withTrimmedRight (bounds.getWidth () - gearRect.getX () + 8);
    // "NAM " in dim ink, "PLAYER" in accent -- drawn as two adjoining runs
    // since JUCE text layout has no inline-colour-span primitive.
    const juce::String namPart = "NAM ", playerPart = "PLAYER";
    const auto font = uiFontTracked (11.0f, true);
    const float w1 = juce::GlyphArrangement::getStringWidth (font, namPart);
    g.setColour (col::inkA (0.55f));
    g.drawText (namPart, nameRect, juce::Justification::centredLeft, false);
    g.setColour (col::accent);
    g.drawText (playerPart, nameRect.withTrimmedLeft ((int)w1), juce::Justification::centredLeft,
                false);
    drawGearIcon (g, gearRect.toFloat ().reduced (10.0f), col::inkA (0.7f));
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

juce::Colour seededHue (const juce::String& seed) {
    const int h = std::abs (seed.hashCode ()) % 100;
    return col::accent.withRotatedHue ((float)h / 100.0f);
}

void drawStompCardChrome (juce::Graphics& g, juce::Rectangle<int> r, juce::Colour hue, bool on) {
    const auto b = r.toFloat ();
    juce::ColourGradient grad (hue.withAlpha (0.28f), b.getX (), b.getY (),
                               hue.darker (0.6f).withAlpha (0.10f), b.getX (), b.getBottom (),
                               false);
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

    // Three decorative knob rings along the card's lower third.
    const float ringD = juce::jmin (16.0f, b.getWidth () * 0.22f);
    const float y = b.getBottom () - ringD - 12.0f;
    const float gap = (b.getWidth () - ringD * 3.0f) / 4.0f;
    g.setColour (col::inkA (0.28f));
    for (int i = 0; i < 3; ++i) {
        const float x = b.getX () + gap + (float)i * (ringD + gap);
        g.drawEllipse (x, y, ringD, ringD, 1.2f);
    }
}

void drawGrilleStrip (juce::Graphics& g, juce::Rectangle<int> r, juce::Colour c) {
    g.saveState ();
    juce::Path clip;
    clip.addRoundedRectangle (r.toFloat (), 8.0f);
    g.reduceClipRegion (clip);
    g.setColour (c);
    constexpr float spacing = 7.0f, thickness = 1.4f;
    const float span = (float)(r.getWidth () + r.getHeight ());
    for (float x = -(float)r.getHeight (); x < span; x += spacing) {
        g.drawLine ((float)r.getX () + x, (float)r.getBottom (),
                    (float)r.getX () + x + r.getHeight (), (float)r.getY (), thickness);
    }
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
