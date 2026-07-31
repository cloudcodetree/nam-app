#pragma once
#include <atomic>
#include <cmath>

namespace dsp {
// Envelope-follower noise gate. Opens (attack) fast, closes (release) slow,
// with hysteresis between open/close thresholds to avoid chatter.
class NoiseGate {
public:
    void prepare(int sampleRate) {
        const float sr = sampleRate > 0 ? (float) sampleRate : 48000.0f;
        envCoeff_  = std::exp(-1.0f / (0.001f * sr));   // 1 ms envelope
        attCoeff_  = std::exp(-1.0f / (0.001f * sr));   // 1 ms open
        relCoeff_  = std::exp(-1.0f / (0.100f * sr));   // 100 ms close
        env_ = 0.0f; gain_ = 1.0f;
    }
    void setEnabled(bool on)      { enabled_.store(on, std::memory_order_relaxed); }
    void setThresholdDb(float db) { threshDb_.store(db, std::memory_order_relaxed); }

    void process(const float* in, float* out, int numSamples) {
        if (! enabled_.load(std::memory_order_relaxed)) {
            if (in != out) for (int i = 0; i < numSamples; ++i) out[i] = in[i];
            return;
        }
        const float openLin  = std::pow(10.0f, threshDb_.load(std::memory_order_relaxed) / 20.0f);
        const float closeLin = openLin * 0.5f;          // -6 dB hysteresis
        for (int i = 0; i < numSamples; ++i) {
            const float x = in[i];
            // Peak envelope follower.
            const float rectified = std::fabs(x);
            env_ = rectified + envCoeff_ * (env_ - rectified);
            // Target gate state with hysteresis.
            if (env_ > openLin)       target_ = 1.0f;
            else if (env_ < closeLin) target_ = 0.0f;   // else hold previous target_
            const float c = target_ > gain_ ? attCoeff_ : relCoeff_;
            gain_ = target_ + c * (gain_ - target_);
            out[i] = x * gain_;
        }
    }
private:
    std::atomic<bool>  enabled_{false};
    std::atomic<float> threshDb_{-60.0f};
    float envCoeff_ = 0.0f, attCoeff_ = 0.0f, relCoeff_ = 0.0f;
    float env_ = 0.0f, gain_ = 1.0f, target_ = 1.0f;
};
} // namespace dsp
