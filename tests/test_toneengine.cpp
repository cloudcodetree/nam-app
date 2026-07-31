// tests/test_toneengine.cpp
#include <catch2/catch_all.hpp>
#include <vector>
#include <cmath>
#include <algorithm>
#include "dsp/ToneEngine.h"

using Catch::Approx;

TEST_CASE("ToneEngine passes audio through when no model") {
    dsp::ToneEngine e; e.prepare(48000, 128);
    e.setInputDb(0.0f); e.setOutputDb(0.0f);
    std::vector<float> in(128), out(128);
    for (int i = 0; i < 128; ++i) in[i] = 0.25f;
    for (int b = 0; b < 40; ++b) e.render(in.data(), out.data(), 128); // settle
    for (int i = 0; i < 128; ++i) REQUIRE(out[i] == Approx(0.25f).epsilon(0.01));
}

TEST_CASE("ToneEngine applies output gain when no model") {
    dsp::ToneEngine e; e.prepare(48000, 128);
    e.setInputDb(0.0f); e.setOutputDb(6.0206f);
    std::vector<float> in(128, 0.1f), out(128);
    for (int b = 0; b < 40; ++b) e.render(in.data(), out.data(), 128);
    REQUIRE(out[64] == Approx(0.2f).epsilon(0.02));
}

TEST_CASE("ToneEngine runs a real model without NaNs") {
    dsp::ToneEngine e; e.prepare(48000, 128);
    auto m = nam::NamModel::load(NAM_FIXTURE_A2, 48000, 128);
    REQUIRE(m != nullptr);
    e.setModel(std::move(m));
    std::vector<float> in(128), out(128);
    for (int i = 0; i < 128; ++i) in[i] = 0.05f * std::sin(i * 0.2f);
    e.render(in.data(), out.data(), 128);
    for (float v : out) REQUIRE(std::isfinite(v));
}

TEST_CASE("ToneEngine handles numSamples greater than maxBlock and reclaims retired models") {
    dsp::ToneEngine e;
    e.prepare(48000, 128);

    auto m1 = nam::NamModel::load(NAM_FIXTURE_A2, 48000, 128);
    REQUIRE(m1 != nullptr);
    e.setModel(std::move(m1));

    std::vector<float> in(256), out(256);
    for (int i = 0; i < 256; ++i) in[i] = 0.05f * std::sin(i * 0.2f);

    // numSamples (256) > maxBlock (128) passed to prepare(): this must not
    // overflow scratch_ (guards the bounds-guard fix) and must stay
    // ASan-clean.
    e.render(in.data(), out.data(), 256);
    for (float v : out) REQUIRE(std::isfinite(v));

    // Load a second model, then call prepare() again to exercise the
    // reclaim-of-retired-models path (the old model is freed off the audio
    // thread in prepare()), and confirm render() still produces finite
    // output afterward.
    auto m2 = nam::NamModel::load(NAM_FIXTURE_A2, 48000, 128);
    REQUIRE(m2 != nullptr);
    e.setModel(std::move(m2));
    e.prepare(48000, 128);

    std::fill(out.begin(), out.end(), 0.0f);
    e.render(in.data(), out.data(), 256);
    for (float v : out) REQUIRE(std::isfinite(v));
}
