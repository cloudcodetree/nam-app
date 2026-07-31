#include <catch2/catch_all.hpp>
#include <vector>
#include <cmath>
#include <algorithm>
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

TEST_CASE("ToneEq flushes denormal state to exact zero after silence") {
    dsp::ToneEq eq; eq.prepare(48000); eq.setEnabled(true);
    eq.setHighDb(6.0f);

    // Short burst of signal to excite the biquad state.
    auto burst = sine(8000, 48000, 512);
    eq.process(burst.data(), (int) burst.size());
    for (float v : burst) REQUIRE(std::isfinite(v));

    // ~200 ms of silence, processed in small blocks (denormal decay is
    // otherwise expected within ~180 ms per the code-review report).
    const int blockSize = 64;
    const int numBlocks = (48000 * 200 / 1000) / blockSize; // 150 blocks
    std::vector<float> block(blockSize, 0.0f);
    for (int b = 0; b < numBlocks; ++b) {
        std::fill(block.begin(), block.end(), 0.0f);
        eq.process(block.data(), (int) block.size());
        for (float v : block) REQUIRE(std::isfinite(v));
    }

    // With state flushed to exact zero once per block, continued zero input
    // must produce an exact 0.0f output, not a lingering subnormal.
    std::vector<float> tail(blockSize, 0.0f);
    eq.process(tail.data(), (int) tail.size());
    for (float v : tail) REQUIRE(v == 0.0f);
}
