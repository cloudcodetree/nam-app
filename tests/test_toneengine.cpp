// tests/test_toneengine.cpp
#include <catch2/catch_all.hpp>
#include <vector>
#include <cmath>
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
