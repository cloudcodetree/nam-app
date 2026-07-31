// tests/test_ircab.cpp
#include <catch2/catch_all.hpp>
#include <vector>
#include <memory>
#include "dsp/IrCab.h"
using Catch::Approx;

TEST_CASE("IrCab disabled is passthrough") {
    dsp::IrCab cab; cab.prepare(48000, 128); cab.setEnabled(false);
    std::vector<float> in(8), out(8);
    for (int i = 0; i < 8; ++i) in[i] = (float) (i + 1);
    cab.process(in.data(), out.data(), 8);
    for (int i = 0; i < 8; ++i) REQUIRE(out[i] == Approx(in[i]));
}

TEST_CASE("IrCab with unit-impulse IR is identity") {
    dsp::IrCab cab; cab.prepare(48000, 128); cab.setEnabled(true);
    cab.setImpulse(std::make_shared<std::vector<float>>(std::vector<float>{1.0f}));
    std::vector<float> in(8), out(8);
    for (int i = 0; i < 8; ++i) in[i] = 0.1f * (i + 1);
    cab.process(in.data(), out.data(), 8);
    for (int i = 0; i < 8; ++i) REQUIRE(out[i] == Approx(in[i]));
}

TEST_CASE("IrCab with 2-tap IR convolves correctly") {
    dsp::IrCab cab; cab.prepare(48000, 128); cab.setEnabled(true);
    cab.setImpulse(std::make_shared<std::vector<float>>(std::vector<float>{0.5f, 0.5f}));
    std::vector<float> in{1, 0, 0, 0}, out(4);
    cab.process(in.data(), out.data(), 4);
    // y[0]=0.5*1, y[1]=0.5*1, rest 0
    REQUIRE(out[0] == Approx(0.5f));
    REQUIRE(out[1] == Approx(0.5f));
    REQUIRE(out[2] == Approx(0.0f));
    REQUIRE(out[3] == Approx(0.0f));
}

TEST_CASE("IrCab handles model-less nullptr IR as passthrough when enabled") {
    dsp::IrCab cab; cab.prepare(48000, 128); cab.setEnabled(true);
    std::vector<float> in(8, 0.2f), out(8);
    cab.process(in.data(), out.data(), 8);   // no IR set yet
    for (int i = 0; i < 8; ++i) REQUIRE(out[i] == Approx(0.2f));
}
