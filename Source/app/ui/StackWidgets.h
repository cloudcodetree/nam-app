#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "model/StackModel.h"

// Small shared visual pieces for the Stacks surface (Home + Detail), split
// out so neither screen file duplicates them. Presentation only.
namespace nam::ui {

// Bottom-anchored accent-bordered toast, auto-dismissing after ~2.2s. Adds a
// self-owned, self-deleting child to `parent` (JUCE detaches a child from
// its parent's list on delete regardless of which dies first, so this is
// safe even if `parent` is torn down before the timer fires).
void showToast (juce::Component& parent, juce::String msg);

// SINGLE / A/B / STEREO outline pill; lime text for non-single routing (a
// stack running two amp chains at once), dim ink otherwise.
void drawRoutingBadge (juce::Graphics&, juce::Rectangle<int>, nam::Stack::Routing);

// Small solid right-pointing triangle -- vector substitute for the "▸"
// unicode glyph (U+25B8), which Work Sans doesn't cover on Android and
// rendered as a bare dot/tofu instead of an arrow. Every "advance/perform"
// affordance (Home's PERFORM pill, PERFORM's NEXT switch) draws this.
void drawFwdTriangle (juce::Graphics&, juce::Rectangle<float>, juce::Colour);

// Circular footswitch-assignment badge (~26px): accent ring + filled digit
// when `fs` is assigned (1..8); dim outline + em-dash otherwise (0).
void drawFsBadge (juce::Graphics&, juce::Rectangle<int>, int fs);

// Stomp-box card chrome (pedal cards + full-width POST rows share this
// body): `art`, if valid, fills the card as a clipped backdrop under a
// tinted legibility wash; otherwise the same accent-tinted gradient fill
// (`on`) / neutral ink wash (bypassed) as before (palette-only -- no
// per-pedal hue rotation). LED dot (glowing lime when `on`, dim otherwise)
// and three knob rings with indicator lines draw on top either way. Caller
// draws name/FS badge on top of this.
void drawStompCardChrome (juce::Graphics&, juce::Rectangle<int>, bool on,
                          const juce::Image& art = {});

// Decorative diagonal hatch/grille strip -- the AMP card's new visual motif.
void drawGrilleStrip (juce::Graphics&, juce::Rectangle<int>, juce::Colour);

// Two ring "speaker cone" glyphs -- the CAB row's motif.
void drawConePair (juce::Graphics&, juce::Rectangle<int>, juce::Colour);

// Gear-artwork thumbnail slot, shared by Home's rig cards, EDIT's AMP/CAB/
// PEDAL/POST cards, and StackGearPicker's rows: draws `img` clipped to a
// rounded rect if valid, otherwise a DELIBERATE placeholder -- palette fill
// + a small vector mark keyed by `type` (amp grille / cab cone / pedal
// stomp-switch / post knob) -- so a missing or not-yet-fetched thumbnail
// never reads as an empty hole or a broken image.
void drawGearThumb (juce::Graphics&, juce::Rectangle<int>, const juce::Image& img,
                    nam::GearType type, float cornerRadius = 10.0f);

}   // namespace nam::ui
