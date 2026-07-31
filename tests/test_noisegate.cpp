// tests/test_noisegate.cpp
#include <catch2/catch_all.hpp>
#include <vector>
#include <cmath>
#include "dsp/NoiseGate.h"
using Catch::Approx;

TEST_CASE("NoiseGate disabled is passthrough") {
    dsp::NoiseGate g; g.prepare(48000); g.setEnabled(false);
    std::vector<float> in(64, 0.3f), out(64, 0.0f);
    g.process(in.data(), out.data(), 64);
    for (int i = 0; i < 64; ++i) REQUIRE(out[i] == Approx(0.3f));
}

TEST_CASE("NoiseGate passes a loud signal above threshold") {
    dsp::NoiseGate g; g.prepare(48000); g.setEnabled(true); g.setThresholdDb(-40.0f);
    std::vector<float> in(4800), out(4800);
    for (int i = 0; i < 4800; ++i) in[i] = 0.5f * std::sin(i * 0.1f); // ~ -6 dB, well above
    g.process(in.data(), out.data(), 4800);
    // After the gate opens, output tracks input closely near the end.
    float e = 0; for (int i = 4700; i < 4800; ++i) e += std::fabs(out[i] - in[i]);
    REQUIRE(e / 100.0f < 0.02f);
}

TEST_CASE("NoiseGate attenuates a quiet signal below threshold") {
    dsp::NoiseGate g; g.prepare(48000); g.setEnabled(true); g.setThresholdDb(-40.0f);
    std::vector<float> in(9600), out(9600);
    for (int i = 0; i < 9600; ++i) in[i] = 0.001f * std::sin(i * 0.1f); // ~ -60 dB, below
    g.process(in.data(), out.data(), 9600);
    float peakOut = 0; for (int i = 9500; i < 9600; ++i) peakOut = std::max(peakOut, std::fabs(out[i]));
    REQUIRE(peakOut < 0.0002f); // heavily attenuated after release
}
