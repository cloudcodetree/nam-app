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

TEST_CASE("NoiseGate in-place aliasing: disabled is exact passthrough") {
    dsp::NoiseGate g; g.prepare(48000); g.setEnabled(false);
    std::vector<float> buf(64);
    for (int i = 0; i < 64; ++i) buf[i] = 0.3f * std::sin(i * 0.2f);
    std::vector<float> ref = buf;
    g.process(buf.data(), buf.data(), 64);
    for (int i = 0; i < 64; ++i) REQUIRE(buf[i] == Approx(ref[i]));
}

TEST_CASE("NoiseGate in-place aliasing: enabled stays finite and consistent") {
    dsp::NoiseGate g; g.prepare(48000); g.setEnabled(true); g.setThresholdDb(-40.0f);
    const int n = 4800;
    std::vector<float> buf(n), ref(n);
    for (int i = 0; i < n; ++i) buf[i] = ref[i] = 0.5f * std::sin(i * 0.1f); // loud, well above threshold
    g.process(buf.data(), buf.data(), n);

    // Compare against an equivalent out-of-place run to make sure aliasing
    // doesn't corrupt the result.
    dsp::NoiseGate g2; g2.prepare(48000); g2.setEnabled(true); g2.setThresholdDb(-40.0f);
    std::vector<float> out(n);
    g2.process(ref.data(), out.data(), n);

    for (int i = 0; i < n; ++i) {
        REQUIRE(std::isfinite(buf[i]));
        REQUIRE(buf[i] == Approx(out[i]));
    }
}

TEST_CASE("NoiseGate holds gate open through hysteresis band on a sustained tone") {
    dsp::NoiseGate g; g.prepare(48000); g.setEnabled(true); g.setThresholdDb(-20.0f); // open~0.1, close~0.05
    const int n = 48000; // ~1 second
    std::vector<float> in(n), out(n);
    // A brief loud "attack" transient (well above openLin) legitimately opens the
    // gate, then the tone decays to a sustain level of 0.07 -- inside the hold
    // band (between closeLin~0.05 and openLin~0.1). The gate starts open
    // (gain 1, target 1); with the fixed peak-follower the envelope holds
    // ~0.07 > closeLin so the gate must STAY OPEN through the sustain.
    for (int i = 0; i < n; ++i) {
        const float amp = i < 2000 ? 0.3f : 0.07f;
        const float w   = i < 2000 ? 0.3f : 0.15f;
        in[i] = amp * std::sin(i * w);
    }
    g.process(in.data(), out.data(), n);
    float e = 0; for (int i = n - 100; i < n; ++i) e += std::fabs(out[i] - in[i]);
    REQUIRE(e / 100.0f < 0.01f); // gate stayed open; no chopping of the sustained tone
}
