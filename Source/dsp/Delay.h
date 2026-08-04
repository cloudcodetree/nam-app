#pragma once
#include <atomic>
#include <vector>
#include <cmath>
#include <algorithm>

namespace dsp {
// Circular-buffer delay with feedback and dry/wet mix.
// Header-only, JUCE-free, RT-safe: prepare() allocates the delay line;
// process() never allocates, locks, or throws.
class Delay {
public:
    static constexpr float kMaxDelaySec = 2.0f;

    void prepare(int sampleRate, int /*maxBlock*/) {
        sr_ = sampleRate > 0 ? (float) sampleRate : 48000.0f;
        line_.assign((size_t) (kMaxDelaySec * sr_) + 1, 0.0f);
        writePos_ = 0;
    }
    void setEnabled(bool on)  { enabled_.store(on, std::memory_order_relaxed); }
    void setTimeMs(float ms)  { timeMs_.store(ms, std::memory_order_relaxed); }
    void setFeedback(float f) { feedback_.store(f, std::memory_order_relaxed); }
    void setMix(float m)      { mix_.store(m, std::memory_order_relaxed); }

    void process(const float* in, float* out, int numSamples) {
        if (! enabled_.load(std::memory_order_relaxed)) {
            if (in != out) for (int i = 0; i < numSamples; ++i) out[i] = in[i];
            return;
        }
        const float timeMs  = timeMs_.load(std::memory_order_relaxed);
        const float feedback = feedback_.load(std::memory_order_relaxed);
        const float mix      = mix_.load(std::memory_order_relaxed);

        const int N = (int) line_.size();
        const int delaySamples = std::clamp((int) (timeMs / 1000.0f * sr_), 1, N - 1);
        const float fb  = std::clamp(feedback, 0.0f, 0.95f);
        const float wet = std::clamp(mix, 0.0f, 1.0f);

        for (int i = 0; i < numSamples; ++i) {
            const float x = in[i]; // read before writing out[i] (in-place aliasing safe)

            int readPos = writePos_ - delaySamples;
            if (readPos < 0) readPos += N;
            const float d = line_[(size_t) readPos];

            out[i] = (1.0f - wet) * x + wet * d;

            float w = x + fb * d;
            if (std::fabs(w) < 1.0e-15f) w = 0.0f; // flush denormal
            line_[(size_t) writePos_] = w;

            ++writePos_;
            if (writePos_ >= N) writePos_ = 0;
        }
    }

private:
    std::atomic<bool>  enabled_{false};
    std::atomic<float> timeMs_{250.0f};
    std::atomic<float> feedback_{0.3f};
    std::atomic<float> mix_{0.3f};

    std::vector<float> line_;
    int   writePos_ = 0;
    float sr_ = 48000.0f;
};
} // namespace dsp
