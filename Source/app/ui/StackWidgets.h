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

// Gear-artwork thumbnail slot, shared by Home's rig cards, EDIT's AMP/CAB/
// PEDAL/POST cards, and StackGearPicker's rows: draws `img` clipped to a
// rounded rect if valid, otherwise a DELIBERATE placeholder -- palette fill
// + a small vector mark keyed by `type` (amp grille / cab cone / pedal
// stomp-switch / post knob) -- so a missing or not-yet-fetched thumbnail
// never reads as an empty hole or a broken image.
void drawGearThumb (juce::Graphics&, juce::Rectangle<int>, const juce::Image& img,
                    nam::GearType type, float cornerRadius = 10.0f);

}   // namespace nam::ui
