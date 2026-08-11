#pragma once
#include <atomic>
#include <memory>
#include <vector>

namespace dsp {
inline constexpr int kMaxIrTaps = 4096;

class IrCab {
public:
    void prepare(int sampleRate, int maxBlock);
    void setEnabled(bool on) { enabled_.store(on, std::memory_order_relaxed); }
    void setImpulse(std::shared_ptr<const std::vector<float>> ir);
    void process(const float* in, float* out, int numSamples);

private:
    std::atomic<bool> enabled_{ false };

    // RT-safe IR ownership: audio thread reads `active_` (raw ptr) only;
    // control thread owns lifetime via current_/retired_, reclaimed in prepare().
    std::atomic<const std::vector<float>*> active_{ nullptr };
    std::shared_ptr<const std::vector<float>> current_;
    std::vector<std::shared_ptr<const std::vector<float>>> retired_;

    // Preallocated delay line (ring buffer) of past inputs; size kMaxIrTaps.
    std::vector<float> ring_;
    int writePos_ = 0;
};
}   // namespace dsp
