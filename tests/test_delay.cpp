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
