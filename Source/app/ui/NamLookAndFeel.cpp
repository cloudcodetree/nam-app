#include "app/ui/NamLookAndFeel.h"
#include "BinaryData.h"

namespace nam::ui {

// --- Font loading (cached once) --------------------------------------------
static juce::Typeface::Ptr caslonTypeface () {
    static juce::Typeface::Ptr tf = juce::Typeface::createSystemTypefaceFor (
        BinaryData::LibreCaslon_ttf, (size_t)BinaryData::LibreCaslon_ttfSize);
    return tf;
}
static juce::Typeface::Ptr workSansTypeface () {
    static juce::Typeface::Ptr tf = juce::Typeface::createSystemTypefaceFor (
        BinaryData::WorkSans_ttf, (size_t)BinaryData::WorkSans_ttfSize);
    return tf;
}

juce::Font displayFont (float height) {
    // FontOptions(Typeface::Ptr) sets name/style from the typeface directly;
    // chaining FontOptions().withTypeface(...) instead would assert in JUCE 9
    // because the default-constructed style ("Regular") is non-empty.
    return juce::Font (juce::FontOptions (caslonTypeface ()).withHeight (height));
}

juce::Font uiFont (float height, bool semibold) {
    juce::Font f (juce::FontOptions (workSansTypeface ()).withHeight (height));
    return semibold ? f.boldened () : f;
}

juce::Font uiFontTracked (float height, bool semibold) {
    // Approximates the design's wide letter-spacing on UPPERCASE micro-labels.
    return uiFont (height, semibold).withExtraKerningFactor (0.16f);
}

// --- Shared hero background -------------------------------------------------
void paintHeroBackground (juce::Graphics& g, juce::Rectangle<int> bounds, bool withGlow) {
    const auto b = bounds.toFloat ();
    g.fillAll (col::bg);

    // radial-gradient(120% 55% at 50% 30%, #191430, #0d0a1c 55%, #08070f)
    const float cx = b.getX () + b.getWidth () * 0.5f;
    const float cy = b.getY () + b.getHeight () * 0.30f;
    const float radius = juce::jmax (b.getWidth (), b.getHeight ()) * 0.9f;
    juce::ColourGradient grad (col::bgGradTop, cx, cy, col::bg, cx + radius, cy, true);
    grad.addColour (0.55, col::bgGradMid);
    g.setGradientFill (grad);
    g.fillRect (b);

    if (withGlow) {
        // radial orange bloom near the top-center
        const float gx = b.getX () + b.getWidth () * 0.5f;
        const float gy = b.getY () + b.getHeight () * 0.33f;
        const float gr = b.getWidth () * 0.7f;
        juce::ColourGradient glow (col::accentA (0.12f), gx, gy, col::accent.withAlpha (0.0f),
                                   gx + gr, gy, true);
        g.setGradientFill (glow);
        g.fillRect (b);
    }
}

void drawPill (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fill, juce::Colour stroke,
               float strokeWidth) {
    const float radius = r.getHeight () * 0.5f;
    if (!fill.isTransparent ()) {
        g.setColour (fill);
        g.fillRoundedRectangle (r, radius);
    }
    if (!stroke.isTransparent () && strokeWidth > 0.0f) {
        g.setColour (stroke);
        g.drawRoundedRectangle (r.reduced (strokeWidth * 0.5f), radius, strokeWidth);
    }
}

juce::String elide (const juce::String& text, const juce::Font& font, int maxWidth) {
    if (maxWidth <= 0) return text;
    if (juce::GlyphArrangement::getStringWidth (font, text) <= maxWidth) return text;
    const juce::String ellipsis = juce::String::fromUTF8 ("\xE2\x80\xA6");   // "…"
    // Binary-search the longest prefix (+ ellipsis) that still fits, rather
    // than shaving one character at a time -- cheap either way at UI-label
    // lengths, but this stays O(log n) if a caller ever elides something long.
    int lo = 0, hi = text.length ();
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        if (juce::GlyphArrangement::getStringWidth (font, text.substring (0, mid) + ellipsis) <=
            maxWidth)
            lo = mid;
        else hi = mid - 1;
    }
    return lo <= 0 ? ellipsis : text.substring (0, lo) + ellipsis;
}

void drawLockGlyph (juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c) {
    g.setColour (c);
    const float bodyH = b.getHeight () * 0.52f;
    const juce::Rectangle<float> body (b.getX () + b.getWidth () * 0.14f, b.getBottom () - bodyH,
                                       b.getWidth () * 0.72f, bodyH);
    g.fillRoundedRectangle (body, body.getWidth () * 0.22f);

    const float rx = b.getWidth () * 0.26f;
    const float ry = b.getHeight () * 0.26f;
    const float cx = b.getCentreX ();
    const float cy = body.getY ();
    juce::Path shackle;
    shackle.addCentredArc (cx, cy, rx, ry, 0.0f, -juce::MathConstants<float>::halfPi,
                           juce::MathConstants<float>::halfPi, true);
    shackle.lineTo (cx + rx, body.getY () + 2.0f);
    shackle.startNewSubPath (cx - rx, cy);
    shackle.lineTo (cx - rx, body.getY () + 2.0f);
    g.strokePath (shackle, juce::PathStrokeType (juce::jmax (1.2f, b.getWidth () * 0.11f)));
}

// --- LookAndFeel ------------------------------------------------------------
NamLookAndFeel::NamLookAndFeel () {
    setColour (juce::Slider::backgroundColourId, col::inkA (0.12f));
    setColour (juce::Slider::trackColourId, col::accent);
    setColour (juce::Slider::thumbColourId, col::accent);
    setColour (juce::Label::textColourId, col::ink);
}

void NamLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float /*minSliderPos*/,
                                       float /*maxSliderPos*/, juce::Slider::SliderStyle,
                                       juce::Slider&) {
    const float trackH = 5.0f;
    const float cy = (float)y + (float)height * 0.5f;
    juce::Rectangle<float> track ((float)x, cy - trackH * 0.5f, (float)width, trackH);

    g.setColour (col::inkA (0.12f));
    g.fillRoundedRectangle (track, trackH * 0.5f);

    // orange fill from the left edge to the thumb
    const float fillW = juce::jlimit ((float)x, (float)(x + width), sliderPos) - (float)x;
    if (fillW > 0.0f) {
        g.setColour (col::accent);
        g.fillRoundedRectangle (track.withWidth (juce::jmax (trackH, fillW)), trackH * 0.5f);
    }

    // round thumb
    const float tr = height * 0.34f;
    g.setColour (col::accent);
    g.fillEllipse (sliderPos - tr, cy - tr, tr * 2.0f, tr * 2.0f);
    g.setColour (col::inkOnAccent.withAlpha (0.25f));
    g.drawEllipse (sliderPos - tr, cy - tr, tr * 2.0f, tr * 2.0f, 1.0f);
}

juce::Typeface::Ptr NamLookAndFeel::getTypefaceForFont (const juce::Font& f) {
    // Default any unspecified font to Work Sans so stray labels stay on-brand.
    juce::ignoreUnused (f);
    return workSansTypeface ();
}

}   // namespace nam::ui
