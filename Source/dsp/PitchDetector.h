#pragma once

namespace dsp {

// Monophonic pitch detection for a tuner (YIN-style difference function
// with cumulative-mean normalisation and parabolic refinement).
// JUCE-free and allocation-free per call (caller provides the window).
//
// Returns the fundamental in Hz, or 0 when no confident pitch is present
// (silence, noise, or out of the guitar/bass range).
float detectPitchHz(const float* samples, int numSamples, double sampleRate);

} // namespace dsp
