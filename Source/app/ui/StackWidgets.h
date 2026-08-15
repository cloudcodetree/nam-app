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

// SETLIST strip chip: accent border/bg/text when active, inkA(.16) border
// otherwise.
void drawSetlistChip (juce::Graphics&, juce::Rectangle<int>, const juce::String& text, bool active);

// Vector gear glyph (Android colour-emoji fallback breaks the unicode ⚙, so
// every "settings" affordance on Stacks draws this instead).
void drawGearIcon (juce::Graphics&, juce::Rectangle<float>, juce::Colour);

// "NAM PLAYER" wordmark + gear icon header row, identical on every Stacks
// state (Home, Detail). `gearRect` is the tap target, in the same
// coordinate space as `bounds`.
void drawStacksBrandHeader (juce::Graphics&, juce::Rectangle<int> bounds,
                            juce::Rectangle<int> gearRect);

// Circular footswitch-assignment badge (~26px): accent ring + filled digit
// when `fs` is assigned (1..8); dim outline + em-dash otherwise (0).
void drawFsBadge (juce::Graphics&, juce::Rectangle<int>, int fs);

// A colour distinct per chain item, derived from `seed` (its uid) by
// rotating the hue of `col::accent` -- no new hex values, just a palette
// transform, so per-pedal variety stays within the "colours from col only"
// rule.
juce::Colour seededHue (const juce::String& seed);

// Stomp-box card chrome (pedal cards + full-width POST rows share this
// body): gradient fill tinted `hue`, LED dot (glowing lime when `on`, dim
// otherwise), three decorative knob rings. Caller draws name/FS badge on
// top of this.
void drawStompCardChrome (juce::Graphics&, juce::Rectangle<int>, juce::Colour hue, bool on);

// Decorative diagonal hatch/grille strip -- the AMP card's new visual motif.
void drawGrilleStrip (juce::Graphics&, juce::Rectangle<int>, juce::Colour);

// Two ring "speaker cone" glyphs -- the CAB row's motif.
void drawConePair (juce::Graphics&, juce::Rectangle<int>, juce::Colour);

}   // namespace nam::ui
