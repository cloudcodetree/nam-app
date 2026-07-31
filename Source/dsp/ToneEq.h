#pragma once
#include <atomic>
#include "dsp/Biquad.h"

namespace dsp {
class ToneEq {
public:
    void prepare(int sampleRate);
    void setEnabled(bool on) { enabled_.store(on, std::memory_order_relaxed); }
    void setLowDb (float db) { lowDb_.store(db,  std::memory_order_relaxed); }
    void setMidDb (float db) { midDb_.store(db,  std::memory_order_relaxed); }
    void setHighDb(float db) { highDb_.store(db, std::memory_order_relaxed); }
    void process(float* buf, int numSamples);
private:
    void recalcIfChanged();
    std::atomic<bool>  enabled_{false};
    std::atomic<float> lowDb_{0.0f}, midDb_{0.0f}, highDb_{0.0f};
    float sr_ = 48000.0f;
    float lastLow_ = 1e9f, lastMid_ = 1e9f, lastHigh_ = 1e9f; // force first recalc
    Biquad low_, mid_, high_;
};
} // namespace dsp
