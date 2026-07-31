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
        return x * current_;
    }
private:
    std::atomic<float> target_{1.0f};
    float current_ = 1.0f;
    float coeff_   = 0.0f;
};
} // namespace dsp
