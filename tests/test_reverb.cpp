// tests/test_reverb.cpp
#include <catch2/catch_all.hpp>
#include <vector>
#include <cmath>
#include "dsp/Reverb.h"
using Catch::Approx;

namespace {
// Sum of squared samples over [from, to) — a proxy for tail energy.
float energy(const std::vector<float>& v, int from, int to) {
    float e = 0.0f;
    for (int i = from; i < to && i < (int) v.size(); ++i) e += v[i] * v[i];
    return e;
}
} // namespace

TEST_CASE("Reverb disabled is passthrough") {
    dsp::Reverb r; r.prepare(48000, 128); r.setEnabled(false);
    const int n = 256;
    std::vector<float> in(n), out(n, 0.0f);
    for (int i = 0; i < n; ++i) in[i] = (float) i * 0.01f;
    r.process(in.data(), out.data(), n);
    for (int i = 0; i < n; ++i) REQUIRE(out[i] == Approx(in[i]));
}

TEST_CASE("Reverb mix=0 is dry") {
    dsp::Reverb r; r.prepare(48000, 128);
    r.setEnabled(true); r.setRoomSize(0.8f); r.setDamping(0.5f); r.setMix(0.0f);
    const int n = 512;
    std::vector<float> in(n, 0.0f), out(n, 0.0f);
    in[0] = 1.0f;
    r.process(in.data(), out.data(), n);
    for (int i = 0; i < n; ++i) REQUIRE(out[i] == Approx(in[i]));
}

TEST_CASE("Reverb process before prepare is safe passthrough") {
    dsp::Reverb r; // no prepare()
    r.setEnabled(true); r.setRoomSize(0.8f); r.setMix(1.0f);
    const int n = 64;
    std::vector<float> in(n), out(n, -1.0f);
    for (int i = 0; i < n; ++i) in[i] = (float) i * 0.01f;
    r.process(in.data(), out.data(), n);
    for (int i = 0; i < n; ++i) REQUIRE(out[i] == Approx(in[i]));
}

TEST_CASE("Reverb impulse produces a decaying tail well past the impulse") {
    dsp::Reverb r; r.prepare(48000, 128);
    r.setEnabled(true); r.setRoomSize(0.8f); r.setDamping(0.3f); r.setMix(1.0f);

    const int n = 48000; // 1 second
    std::vector<float> in(n, 0.0f), out(n, 0.0f);
    in[0] = 1.0f;
    r.process(in.data(), out.data(), n);

    for (float v : out) REQUIRE(std::isfinite(v));

    // Energy present hundreds of ms after the impulse (a real reverberant tail).
    const float late = energy(out, 14400, 18400); // ~300-380 ms
    REQUIRE(late > 1.0e-6f);

    // Tail decays: a later window carries less energy than an earlier one.
    const float early = energy(out, 2400, 6400);   // ~50-133 ms
    const float later = energy(out, 33600, 37600);  // ~700-783 ms
    REQUIRE(later < early);
}

TEST_CASE("Reverb larger room size yields a longer/louder tail") {
    const int n = 48000;
    auto tailEnergy = [n](float roomSize) {
        dsp::Reverb r; r.prepare(48000, 128);
        r.setEnabled(true); r.setRoomSize(roomSize); r.setDamping(0.2f); r.setMix(1.0f);
        std::vector<float> in(n, 0.0f), out(n, 0.0f);
        in[0] = 1.0f;
        r.process(in.data(), out.data(), n);
        return energy(out, 24000, 28000); // ~500-583 ms
    };
    REQUIRE(tailEnergy(0.9f) > tailEnergy(0.3f));
}

TEST_CASE("Reverb stays finite and decays over long silence") {
    dsp::Reverb r; r.prepare(48000, 128);
    r.setEnabled(true); r.setRoomSize(0.9f); r.setDamping(0.4f); r.setMix(1.0f);

    const int burst = 256;
    const int silence = 192000; // 4 s
    std::vector<float> in(burst + silence, 0.0f), out(burst + silence, 0.0f);
    for (int i = 0; i < burst; ++i) in[i] = 0.5f * std::sin((float) i * 0.3f);

    r.process(in.data(), out.data(), (int) in.size());

    for (float v : out) REQUIRE(std::isfinite(v));
    // The tail decays: energy near the end is a tiny fraction of the energy
    // just after the burst. (Relative, so it doesn't bake in an exact RT60 —
    // a high-feedback room legitimately rings for seconds.)
    const float earlyTail = energy(out, burst, burst + 4000);
    const float endTail    = energy(out, (int) out.size() - 4000, (int) out.size());
    REQUIRE(earlyTail > 0.0f);
    REQUIRE(endTail < earlyTail * 1.0e-3f);
}
