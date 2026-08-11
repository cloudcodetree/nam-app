#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Everything the audio configuration UI shows, supplied by the host app.
// (Displayed by the status-orb flyout: device rows, ENGINE rate/buffer
// pills, round-trip latency.)
struct AudioSettingsState {
    juce::StringArray inputs, outputs;
    juce::String currentInput, currentOutput;
    // Where "System Default" output actually lands right now (e.g. the USB
    // interface) — kept so default routing can be shown honestly.
    juce::String outputRouteHint;
    juce::StringArray rates, buffers;   // chip labels, e.g. "48k" / "192"
    juce::String currentRate, currentBuffer;
    double latencyMs = 0.0;   // round-trip estimate
    bool running = false;
};
