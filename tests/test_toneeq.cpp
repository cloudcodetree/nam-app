#include <catch2/catch_all.hpp>
#include <vector>
#include <cmath>
#include "dsp/ToneEq.h"
using Catch::Approx;

static float rms(const std::vector<float>& v, int from) {
    double s = 0; int n = 0;
    for (int i = from; i < (int) v.size(); ++i) { s += (double) v[i]*v[i]; ++n; }
    return (float) std::sqrt(s / n);
}
static std::vector<float> sine(float freq, float sr, int n) {
    std::vector<float> v(n);
    for (int i = 0; i < n; ++i) v[i] = std::sin(2.0f*3.14159265f*freq*i/sr);
    return v;
}

TEST_CASE("ToneEq disabled is passthrough") {
    dsp::ToneEq eq; eq.prepare(48000); eq.setEnabled(false);
    auto in = sine(1000, 48000, 512); auto buf = in;
    eq.process(buf.data(), (int) buf.size());
    for (size_t i = 0; i < buf.size(); ++i) REQUIRE(buf[i] == Approx(in[i]));
}

TEST_CASE("ToneEq flat (0 dB) is ~unity") {
    dsp::ToneEq eq; eq.prepare(48000); eq.setEnabled(true);
    eq.setLowDb(0); eq.setMidDb(0); eq.setHighDb(0);
    auto in = sine(1000, 48000, 4096); auto buf = in;
    eq.process(buf.data(), (int) buf.size());
    REQUIRE(rms(buf, 2048) == Approx(rms(in, 2048)).epsilon(0.02));
}

TEST_CASE("ToneEq high-shelf boost increases HF energy") {
    dsp::ToneEq eq; eq.prepare(48000); eq.setEnabled(true);
    eq.setHighDb(12.0f);
    auto in = sine(8000, 48000, 8192); auto buf = in;
    eq.process(buf.data(), (int) buf.size());
    REQUIRE(rms(buf, 4096) > rms(in, 4096) * 1.5f);
}
