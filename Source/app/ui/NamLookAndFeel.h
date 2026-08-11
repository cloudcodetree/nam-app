#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// NAM Player "Hi-Fi" design-system foundation (from the Claude Design mockups).
// Palette + bundled fonts + custom drawing so every screen shares one look:
// deep purple-black, burnt-orange accent, Libre Caslon display serif + Work
// Sans UI, faders (not knobs), pill buttons, glowing tone cards, gradient meters.
namespace nam::ui {

// --- Palette (exact values from the design) --------------------------------
namespace col {
    const juce::Colour bg           { 0xff08070f }; // base background
    const juce::Colour bgGradTop    { 0xff191430 }; // hero radial inner
    const juce::Colour bgGradMid    { 0xff0d0a1c };
    const juce::Colour ink          { 0xfff2eee6 }; // primary warm off-white
    const juce::Colour accent       { 0xffff4d00 }; // burnt orange
    const juce::Colour accentAlt    { 0xffff6a2b };
    const juce::Colour accentHover  { 0xffff7a40 };
    const juce::Colour inkOnAccent  { 0xff100a06 }; // near-black text on orange
    const juce::Colour meterGreen   { 0xff3fae6a };
    const juce::Colour meterLime    { 0xffc8f051 };
    const juce::Colour meterBlue    { 0xff4da3ff }; // output meters (vs lime input)

    inline juce::Colour inkA (float a) { return ink.withAlpha (a); }        // dimmed ink
    inline juce::Colour accentA (float a) { return accent.withAlpha (a); }  // orange washes
}

// --- Fonts (loaded once from bundled BinaryData) ---------------------------
// display: Libre Caslon Display (serif) — tone names, screen titles.
// ui:      Work Sans — everything else. weight 400/500/600 via style flags.
juce::Font displayFont (float height);
juce::Font uiFont (float height, bool semibold = false);
juce::Font uiFontTracked (float height, bool semibold);   // for UPPERCASE micro-labels

// Paint the shared hero background (radial purple gradient + soft orange glow).
void paintHeroBackground (juce::Graphics& g, juce::Rectangle<int> bounds, bool withGlow = true);

// A pill / rounded-rect helper: fill + optional 1px stroke.
void drawPill (juce::Graphics& g, juce::Rectangle<float> r,
               juce::Colour fill, juce::Colour stroke, float strokeWidth = 1.0f);

// --- LookAndFeel: faders, and default typeface -----------------------------
class NamLookAndFeel : public juce::LookAndFeel_V4 {
public:
    NamLookAndFeel();

    // Horizontal fader: thin track, orange fill to the thumb, round thumb.
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
};

} // namespace nam::ui
