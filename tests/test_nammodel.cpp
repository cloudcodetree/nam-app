#include <catch2/catch_all.hpp>
#include <cmath>
#include <vector>
#include <fstream>
#include <filesystem>
#include "model/NamModel.h"

using Catch::Approx;

#ifndef NAM_FIXTURE_A2
#define NAM_FIXTURE_A2 "tests/fixtures/example_a2.nam"
#endif

TEST_CASE("NamModel loads a valid A2 file") {
    auto m = nam::NamModel::load(NAM_FIXTURE_A2, 48000, 128);
    REQUIRE(m != nullptr);
    REQUIRE(m->sampleRate() == 48000);
}

#ifdef NAM_MODEL_A2_REAL
TEST_CASE("NamModel loads and processes a real A2 amp capture (SlimmableContainer)") {
    // models/A2.nam is a genuine A2 capture (architecture "SlimmableContainer",
    // the production A2 format), not the synthetic conformance fixture. This
    // pins down that our NeuralAudio build actually runs real A2 tones.
    auto m = nam::NamModel::load(NAM_MODEL_A2_REAL, 48000, 128);
    REQUIRE(m != nullptr);
    REQUIRE(m->sampleRate() == 48000);

    std::vector<float> in(128), out(128);
    for (int i = 0; i < 128; ++i) in[i] = 0.05f * std::sin(i * 0.2f);
    m->process(in.data(), out.data(), 128);
    for (float v : out) REQUIRE(std::isfinite(v));
}
#endif

TEST_CASE("NamModel returns nullptr on bad path") {
    auto m = nam::NamModel::load("does/not/exist.nam", 48000, 128);
    REQUIRE(m == nullptr);
}

TEST_CASE("NamModel returns nullptr on a corrupt existing file") {
    auto path = std::filesystem::temp_directory_path() / "nam_corrupt_test.nam";
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        f << "this is not valid json / nam model content {{{";
    }

    auto m = nam::NamModel::load(path.string(), 48000, 128);
    REQUIRE(m == nullptr);

    std::filesystem::remove(path);
}

TEST_CASE("NamModel processing is deterministic") {
    auto m = nam::NamModel::load(NAM_FIXTURE_A2, 48000, 128);
    REQUIRE(m != nullptr);
    std::vector<float> in(128), a(128), b(128);
    for (int i = 0; i < 128; ++i) in[i] = 0.1f * std::sin(i * 0.1f);
    m->process(in.data(), a.data(), 128);
    // Reload to reset internal state, process the same input again.
    auto m2 = nam::NamModel::load(NAM_FIXTURE_A2, 48000, 128);
    m2->process(in.data(), b.data(), 128);
    for (int i = 0; i < 128; ++i) REQUIRE(a[i] == Approx(b[i]));
}

TEST_CASE("NamModel silence in stays bounded") {
    auto m = nam::NamModel::load(NAM_FIXTURE_A2, 48000, 128);
    std::vector<float> in(128, 0.0f), out(128, 0.0f);
    m->process(in.data(), out.data(), 128);
    for (float v : out) REQUIRE(std::isfinite(v));
    // NOTE: this fixture (wavenet_a2_max.nam) is a synthetic A2 conformance
    // test model (its own metadata: "meant as a 'test case' ... generated
    // WaveNet model"), not a trained amp capture. Measured behavior for
    // silence-in is a stable, deterministic DC bias (verified via a
    // standalone NeuralAudio diagnostic outside this wrapper), not a runaway.
    // This is a characterization assertion: it pins that exact deterministic
    // constant (measured as 9.26655483f) with a tight tolerance, so any
    // regression that perturbs the fixture's behavior is caught, while the
    // isfinite check above remains the "doesn't blow up" guarantee.
    constexpr float kExpectedSilenceDcBias = 9.26655f;
    constexpr float kTolerance = 0.05f;
    for (float v : out) REQUIRE(std::abs(v - kExpectedSilenceDcBias) < kTolerance);
}
