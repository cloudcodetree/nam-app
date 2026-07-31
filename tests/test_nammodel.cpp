#include <catch2/catch_all.hpp>
#include <cmath>
#include <vector>
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

TEST_CASE("NamModel returns nullptr on bad path") {
    auto m = nam::NamModel::load("does/not/exist.nam", 48000, 128);
    REQUIRE(m == nullptr);
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
    // silence-in is a stable, deterministic ~9.27 DC bias (verified via a
    // standalone NeuralAudio diagnostic outside this wrapper), not a runaway.
    // The bound below is set with headroom above that observed value; the
    // real "doesn't blow up" guarantee is the isfinite check above.
    for (float v : out) REQUIRE(std::abs(v) < 15.0f);
}
