#include "dsp/Reverb.h"

#include <algorithm>
#include <cmath>

namespace dsp {
namespace {
// Jezar's Freeverb tunings, in samples at 44.1 kHz. prepare() scales these to
// the actual sample rate so the reverb sounds the same at any rate.
constexpr int kCombTuning[8]    = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
constexpr int kAllpassTuning[4] = {556, 441, 341, 225};

constexpr float kFixedGain = 0.015f; // input scale that keeps 8 high-feedback combs bounded
constexpr float kScaleRoom = 0.28f;  // roomSize -> feedback maps to 0.70 .. 0.98
constexpr float kOffsetRoom = 0.70f;
constexpr float kScaleDamp = 0.40f;  // damping  -> comb lowpass amount
} // namespace

void Reverb::prepare(int sampleRate, int /*maxBlock*/) {
    const float sr = sampleRate > 0 ? (float) sampleRate : 48000.0f;
    const float scale = sr / 44100.0f;

    for (int i = 0; i < kNumCombs; ++i) {
        const int len = std::max(1, (int) std::lround(kCombTuning[i] * scale));
        combs_[i].buf.assign((size_t) len, 0.0f);
        combs_[i].reset();
    }
    for (int i = 0; i < kNumAllpasses; ++i) {
        const int len = std::max(1, (int) std::lround(kAllpassTuning[i] * scale));
        allpasses_[i].buf.assign((size_t) len, 0.0f);
        allpasses_[i].reset();
    }
    prepared_ = true;
}

void Reverb::process(const float* in, float* out, int numSamples) {
    // Disabled, or prepare() never ran: transparent passthrough.
    if (! enabled_.load(std::memory_order_relaxed) || ! prepared_) {
        if (in != out) for (int i = 0; i < numSamples; ++i) out[i] = in[i];
        return;
    }

    const float room = std::clamp(roomSize_.load(std::memory_order_relaxed), 0.0f, 1.0f);
    const float damp = std::clamp(damping_.load(std::memory_order_relaxed), 0.0f, 1.0f);
    const float wet  = std::clamp(mix_.load(std::memory_order_relaxed), 0.0f, 1.0f);

    const float feedback = room * kScaleRoom + kOffsetRoom; // 0.70 .. 0.98
    const float damp1 = damp * kScaleDamp;                  // lowpass coefficient
    const float damp2 = 1.0f - damp1;

    for (int i = 0; i < numSamples; ++i) {
        const float x = in[i];                 // read before writing out[i] (in-place safe)
        const float input = x * kFixedGain;

        float acc = 0.0f;
        for (int c = 0; c < kNumCombs; ++c)
            acc += combs_[c].process(input, feedback, damp1, damp2);
        for (int a = 0; a < kNumAllpasses; ++a)
            acc = allpasses_[a].process(acc);

        out[i] = (1.0f - wet) * x + wet * acc;
    }
}
} // namespace dsp
