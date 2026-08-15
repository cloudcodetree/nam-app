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

}   // namespace nam::ui
