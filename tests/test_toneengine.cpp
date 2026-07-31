// tests/test_toneengine.cpp
#include <catch2/catch_all.hpp>
#include <vector>
#include <cmath>
#include <cstdint>
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

TEST_CASE("ToneEngine clamps render to the active model's max block during a buffer-size increase") {
    // Reproduces the hazardous window: prepare() resizes scratch_ for a new
    // (larger) device buffer size immediately, but the model reload is
    // asynchronous, so the OLD model - still prepared/asserting on the
    // SMALLER maxBlock - remains active for a short time. render() must not
    // feed that model more frames than it was prepared for.
    dsp::ToneEngine e;
    e.prepare(48000, 128);

    auto m = nam::NamModel::load(NAM_FIXTURE_A2, 48000, 128);
    REQUIRE(m != nullptr);
    REQUIRE(m->maxBlock() == 128);
    e.setModel(std::move(m));

    // Simulate a device buffer-size increase without reloading the model:
    // prepare() clears retired_ and resizes scratch_ to 512, but does NOT
    // change the active model, so the old 128-capacity model stays active.
    e.prepare(48000, 512);

    std::vector<float> in(512), out(512);
    for (int i = 0; i < 512; ++i) in[i] = 0.05f * std::sin(i * 0.2f);

    e.render(in.data(), out.data(), 512);
    for (float v : out) REQUIRE(std::isfinite(v));
}

TEST_CASE("ToneEngine reports lock-free telemetry") {
    dsp::ToneEngine e;
    e.prepare(48000, 128);
    e.setInputDb(0.0f); e.setOutputDb(0.0f);

    // Peak reflects the output level; blockCount increments; load is sane.
    std::vector<float> in(128, 0.5f), out(128);
    for (int b = 0; b < 40; ++b) e.render(in.data(), out.data(), 128); // settle gains
    REQUIRE(e.blockCount() == 40u);
    REQUIRE(e.outputPeak() == Approx(0.5f).epsilon(0.02));
    REQUIRE(e.cpuLoad() >= 0.0f);
    REQUIRE(std::isfinite(e.cpuLoad()));
    REQUIRE(e.overCapacityCount() == 0u);

    // Over-capacity counter increments when a block exceeds the active
    // model's prepared capacity (the buffer-size-increase window).
    auto m = nam::NamModel::load(NAM_FIXTURE_A2, 48000, 128);
    REQUIRE(m != nullptr);
    e.setModel(std::move(m));
    e.prepare(48000, 512);                 // grow scratch_, keep old 128-cap model
    std::vector<float> big_in(512, 0.1f), big_out(512);
    const uint32_t before = e.overCapacityCount();
    e.render(big_in.data(), big_out.data(), 512);
    REQUIRE(e.overCapacityCount() == before + 1u);
}

TEST_CASE("ToneEngine chain: gate+IR+EQ all bypassed equals Phase-1 behavior") {
    dsp::ToneEngine e; e.prepare(48000, 128);
    e.setInputDb(0.0f); e.setOutputDb(0.0f);
    // all stages default-disabled; passthrough with no model
    std::vector<float> in(128, 0.25f), out(128);
    for (int b = 0; b < 40; ++b) e.render(in.data(), out.data(), 128);
    for (int i = 0; i < 128; ++i) REQUIRE(out[i] == Catch::Approx(0.25f).epsilon(0.01));
}

TEST_CASE("ToneEngine chain with a unit-impulse IR is transparent") {
    dsp::ToneEngine e; e.prepare(48000, 128);
    e.setInputDb(0.0f); e.setOutputDb(0.0f);
    e.setIrEnabled(true);
    e.setImpulse(std::make_shared<std::vector<float>>(std::vector<float>{1.0f}));
    std::vector<float> in(128, 0.2f), out(128);
    for (int b = 0; b < 40; ++b) e.render(in.data(), out.data(), 128);
    for (int i = 0; i < 128; ++i) REQUIRE(std::isfinite(out[i]));
    REQUIRE(out[64] == Catch::Approx(0.2f).epsilon(0.02));
}

TEST_CASE("ToneEngine chain with a real model + gate + EQ stays finite") {
    dsp::ToneEngine e; e.prepare(48000, 128);
    auto m = nam::NamModel::load(NAM_FIXTURE_A2, 48000, 128);
    REQUIRE(m != nullptr);
    e.setModel(std::move(m));
    e.setGateEnabled(true); e.setGateThresholdDb(-50.0f);
    e.setEqEnabled(true); e.setHighDb(6.0f); e.setLowDb(3.0f);
    std::vector<float> in(128), out(128);
    for (int i = 0; i < 128; ++i) in[i] = 0.05f * std::sin(i * 0.2f);
    for (int b = 0; b < 8; ++b) e.render(in.data(), out.data(), 128);
    for (float v : out) REQUIRE(std::isfinite(v));
}

TEST_CASE("ToneEngine full chain: model + gate + IR cab + EQ all active stays finite") {
    // The exact end-to-end integration Phase 2 delivers: every stage on, with
    // a real A2 model. Guards against a regression where two stages interact
    // badly (buffer sizing, ordering) that single-stage tests would miss.
    dsp::ToneEngine e; e.prepare(48000, 128);
    auto m = nam::NamModel::load(NAM_FIXTURE_A2, 48000, 128);
    REQUIRE(m != nullptr);
    e.setModel(std::move(m));
    e.setGateEnabled(true);  e.setGateThresholdDb(-55.0f);
    e.setIrEnabled(true);    e.setImpulse(std::make_shared<std::vector<float>>(
                                 std::vector<float>{0.5f, 0.3f, 0.2f}));
    e.setEqEnabled(true);    e.setLowDb(3.0f); e.setMidDb(-2.0f); e.setHighDb(4.0f);
    std::vector<float> in(128), out(128);
    for (int i = 0; i < 128; ++i) in[i] = 0.1f * std::sin(i * 0.15f);
    for (int b = 0; b < 16; ++b) e.render(in.data(), out.data(), 128);
    for (float v : out) REQUIRE(std::isfinite(v));
    REQUIRE(e.overCapacityCount() == 0u);   // no clamp events on a well-sized chain
}
