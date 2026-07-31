// tests/test_gain.cpp
#include <catch2/catch_all.hpp>
#include "dsp/Gain.h"

using Catch::Approx;

TEST_CASE("Gain of 0 dB is unity after settle") {
    dsp::Gain g; g.reset(1.0f, 48000); g.setDb(0.0f);
    float y = 0; for (int i = 0; i < 4800; ++i) y = g.applyNext(1.0f);
    REQUIRE(y == Approx(1.0f).epsilon(0.001));
}

TEST_CASE("Gain of +6 dB settles near 2x") {
    dsp::Gain g; g.reset(1.0f, 48000); g.setDb(6.0206f);
    float y = 0; for (int i = 0; i < 4800; ++i) y = g.applyNext(1.0f);
    REQUIRE(y == Approx(2.0f).epsilon(0.01));
}

TEST_CASE("Gain smooths — no instantaneous jump") {
    dsp::Gain g; g.reset(1.0f, 48000); g.setDb(20.0f);
    float first = g.applyNext(1.0f);
    REQUIRE(first < 3.0f); // not the full 10x on sample 1
}
