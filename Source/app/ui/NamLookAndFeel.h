#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// NAM Player "Hi-Fi" design-system foundation (from the Claude Design mockups).
// Palette + bundled fonts + custom drawing so every screen shares one look:
// deep purple-black, burnt-orange accent, Libre Caslon display serif + Work
// Sans UI, faders (not knobs), pill buttons, glowing tone cards, gradient meters.
namespace nam::ui {

// --- Palette (exact values from the design) --------------------------------
namespace col {
const juce::Colour bg{ 0xff08070f };          // base background
const juce::Colour bgGradTop{ 0xff191430 };   // hero radial inner
const juce::Colour bgGradMid{ 0xff0d0a1c };
const juce::Colour ink{ 0xfff2eee6 };      // primary warm off-white
const juce::Colour accent{ 0xffff4d00 };   // burnt orange
const juce::Colour accentAlt{ 0xffff6a2b };
const juce::Colour accentHover{ 0xffff7a40 };
const juce::Colour inkOnAccent{ 0xff100a06 };   // near-black text on orange
const juce::Colour meterGreen{ 0xff3fae6a };
const juce::Colour meterLime{ 0xffc8f051 };
const juce::Colour meterBlue{ 0xff4da3ff };   // output meters (vs lime input)
// Overlay tokens (was inline 0xa008070f/0xf214101f hex in StackGearPicker,
// StackItemSheet, StackEditViewPaint, PaywallPanel, PlayScreen,
// AppShellChrome) -- one named pair so every flyout/sheet/scrim shares it.
const juce::Colour scrim{ 0xa008070f };     // full-screen dim behind a sheet/overlay
const juce::Colour sheetBg{ 0xf214101f };   // the sheet/flyout panel itself

inline juce::Colour inkA (float a) { return ink.withAlpha (a); }         // dimmed ink
inline juce::Colour accentA (float a) { return accent.withAlpha (a); }   // orange washes
}   // namespace col

// --- Fonts (loaded once from bundled BinaryData) ---------------------------
// display: Libre Caslon Display (serif) — tone names, screen titles.
// ui:      Work Sans — everything else. weight 400/500/600 via style flags.
juce::Font displayFont (float height);
juce::Font uiFont (float height, bool semibold = false);
juce::Font uiFontTracked (float height, bool semibold);   // for UPPERCASE micro-labels

// Paint the shared hero background (radial purple gradient + soft orange glow).
void paintHeroBackground (juce::Graphics& g, juce::Rectangle<int> bounds, bool withGlow = true);

// A pill / rounded-rect helper: fill + optional 1px stroke.
void drawPill (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fill, juce::Colour stroke,
               float strokeWidth = 1.0f);

// Elides `text` to fit `maxWidth` px in `font`, appending "..." (JUCE's own
// ellipsis glyph via String::getEllipsisString) -- shared helper so text
// truncation isn't re-solved (and re-broken) in every screen that draws a
// label into a fixed-width rect. Returns `text` unchanged if it already
// fits or `maxWidth` is non-positive.
juce::String elide (const juce::String& text, const juce::Font& font, int maxWidth);

// Vector padlock (rounded-rect body + arc shackle) for gated Pro rows —
// screens that gate a feature draw this instead of an emoji/unicode glyph.
void drawLockGlyph (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c);

// --- LookAndFeel: faders, and default typeface -----------------------------
class NamLookAndFeel : public juce::LookAndFeel_V4 {
public:
    NamLookAndFeel ();

    // Horizontal fader: thin track, orange fill to the thumb, round thumb.
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle,
                           juce::Slider&) override;

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
};

}   // namespace nam::ui
