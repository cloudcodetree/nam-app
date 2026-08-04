// tests/test_delay.cpp
#include <catch2/catch_all.hpp>
#include <vector>
#include <cmath>
#include "dsp/Delay.h"
using Catch::Approx;

TEST_CASE("Delay disabled is passthrough") {
    dsp::Delay d; d.prepare(48000, 128); d.setEnabled(false);
    const int n = 256;
    std::vector<float> in(n), out(n, 0.0f);
    for (int i = 0; i < n; ++i) in[i] = (float) i * 0.01f;
    d.process(in.data(), out.data(), n);
    for (int i = 0; i < n; ++i) REQUIRE(out[i] == Approx(in[i]));
}

TEST_CASE("Delay delays an impulse") {
    dsp::Delay d; d.prepare(48000, 128);
    d.setEnabled(true); d.setTimeMs(10.0f); d.setFeedback(0.0f); d.setMix(1.0f);

    const int n = 1024;
    std::vector<float> in(n, 0.0f), out(n, 0.0f);
    in[0] = 1.0f;
    d.process(in.data(), out.data(), n);

    const int expected = (int) std::round(0.010 * 48000.0); // 480
    REQUIRE(out[expected] == Approx(1.0f).margin(0.01f));
    for (int i = 0; i < expected; ++i) REQUIRE(std::fabs(out[i]) < 1.0e-6f);
}

TEST_CASE("Delay feedback produces decaying echoes") {
    dsp::Delay d; d.prepare(48000, 128);
    d.setEnabled(true); d.setTimeMs(10.0f); d.setFeedback(0.5f); d.setMix(1.0f);

    const int n = 2048;
    std::vector<float> in(n, 0.0f), out(n, 0.0f);
    in[0] = 1.0f;
    d.process(in.data(), out.data(), n);

    const int first  = (int) std::round(0.010 * 48000.0);       // 480
    const int second = first * 2;                                // 960
    REQUIRE(out[first] == Approx(1.0f).margin(0.01f));
    REQUIRE(out[second] == Approx(0.5f).margin(0.02f));
}

TEST_CASE("Delay mix=0 is dry") {
    dsp::Delay d; d.prepare(48000, 128);
    d.setEnabled(true); d.setTimeMs(10.0f); d.setFeedback(0.3f); d.setMix(0.0f);

    const int n = 256;
    std::vector<float> in(n), out(n, 0.0f);
    for (int i = 0; i < n; ++i) in[i] = (float) i * 0.01f;
    d.process(in.data(), out.data(), n);
    for (int i = 0; i < n; ++i) REQUIRE(out[i] == Approx(in[i]));
}

TEST_CASE("Delay time beyond kMaxDelaySec clamps without overrun") {
    dsp::Delay d; d.prepare(48000, 128);
    d.setEnabled(true); d.setFeedback(0.0f); d.setMix(1.0f);
    d.setTimeMs(10000.0f); // 10s requested; line holds only 2s

    const int n = 2048;
    std::vector<float> in(n, 0.0f), out(n, 0.0f);
    in[0] = 1.0f;
    // Must not read/write out of bounds; delaySamples clamps to line_.size()-1.
    d.process(in.data(), out.data(), n);
    for (float v : out) REQUIRE(std::isfinite(v));
}

TEST_CASE("Delay feedback clamps at 0.95") {
    dsp::Delay d; d.prepare(48000, 128);
    d.setEnabled(true); d.setTimeMs(10.0f); d.setFeedback(5.0f); d.setMix(1.0f);

    const int n = 2048;
    std::vector<float> in(n, 0.0f), out(n, 0.0f);
    in[0] = 1.0f;
    d.process(in.data(), out.data(), n);

    const int first  = (int) std::round(0.010 * 48000.0); // 480
    const int second = first * 2;                          // 960
    // Feedback clamped to 0.95, so the second echo is 0.95, not 5.0.
    REQUIRE(out[first]  == Approx(1.0f).margin(0.01f));
    REQUIRE(out[second] == Approx(0.95f).margin(0.02f));
}

TEST_CASE("Delay mix clamps to [0,1]") {
    dsp::Delay d; d.prepare(48000, 128);
    d.setEnabled(true); d.setTimeMs(10.0f); d.setFeedback(0.0f); d.setMix(5.0f);

    const int n = 1024;
    std::vector<float> in(n, 0.0f), out(n, 0.0f);
    in[0] = 1.0f;
    d.process(in.data(), out.data(), n);

    const int expected = (int) std::round(0.010 * 48000.0); // 480
    // mix clamps to 1.0 (fully wet): dry sample at 0 is gone, wet spike at 480.
    REQUIRE(std::fabs(out[0]) < 1.0e-6f);
    REQUIRE(out[expected] == Approx(1.0f).margin(0.01f));
}

TEST_CASE("Delay process before prepare is safe passthrough") {
    dsp::Delay d; // no prepare()
    d.setEnabled(true); d.setTimeMs(10.0f); d.setFeedback(0.5f); d.setMix(1.0f);

    const int n = 64;
    std::vector<float> in(n), out(n, -1.0f);
    for (int i = 0; i < n; ++i) in[i] = (float) i * 0.01f;
    // Empty line_ must not clamp(1,-1) or index OOB; falls through to passthrough.
    d.process(in.data(), out.data(), n);
    for (int i = 0; i < n; ++i) REQUIRE(out[i] == Approx(in[i]));
}

TEST_CASE("Delay re-prepare resets state and resizes") {
    dsp::Delay d; d.prepare(48000, 128);
    d.setEnabled(true); d.setTimeMs(10.0f); d.setFeedback(0.9f); d.setMix(1.0f);

    // Prime the line with an impulse + echoes.
    const int n = 2048;
    std::vector<float> in(n, 0.0f), out(n, 0.0f);
    in[0] = 1.0f;
    d.process(in.data(), out.data(), n);

    // Re-prepare at a different rate: line must be re-zeroed and writePos reset,
    // so a fresh silent block comes out silent (no stale echoes bleed through).
    d.prepare(44100, 128);
    std::vector<float> silence(n, 0.0f), out2(n, 0.0f);
    d.process(silence.data(), out2.data(), n);
    for (float v : out2) REQUIRE(std::fabs(v) < 1.0e-6f);
}

TEST_CASE("Delay stays finite over long silence") {
    dsp::Delay d; d.prepare(48000, 128);
    d.setEnabled(true); d.setTimeMs(10.0f); d.setFeedback(0.9f); d.setMix(1.0f);

    const int burst = 256;
    const int silence = 96000;
    std::vector<float> in(burst + silence, 0.0f), out(burst + silence, 0.0f);
    for (int i = 0; i < burst; ++i) in[i] = 0.5f * std::sin((float) i * 0.3f);

    d.process(in.data(), out.data(), (int) in.size());

    for (float v : out) REQUIRE(std::isfinite(v));
}
