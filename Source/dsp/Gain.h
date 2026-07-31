#pragma once
#include <cmath>
#include <atomic>

namespace dsp {
class Gain {
public:
    void reset(float linear, int sampleRate) {
        target_.store(linear, std::memory_order_relaxed);
        current_ = linear;
        // ~5 ms smoothing
        const float t = 0.005f * static_cast<float>(sampleRate);
        coeff_ = t > 1.0f ? std::exp(-1.0f / t) : 0.0f;
    }
    void setDb(float db) {
        target_.store(std::pow(10.0f, db / 20.0f), std::memory_order_relaxed);
    }
    float applyNext(float x) {
        const float tgt = target_.load(std::memory_order_relaxed);
        current_ = tgt + coeff_ * (current_ - tgt);
        // Anti-denormal flush: during long silence current_ can decay into
        // subnormal territory, which is extremely slow on some FPUs. This
        // add/subtract round-trip is a no-op numerically but forces the
        // value back to zero/normal range instead of lingering subnormal.
        current_ += 1.0e-20f; current_ -= 1.0e-20f;
        return x * current_;
    }
private:
    std::atomic<float> target_{1.0f};
    float current_ = 1.0f;
    float coeff_   = 0.0f;
};
} // namespace dsp
