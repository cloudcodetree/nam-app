#include "dsp/ToneEq.h"

namespace dsp {

void ToneEq::prepare(int sampleRate) {
    sr_ = sampleRate > 0 ? (float) sampleRate : 48000.0f;
    lastLow_ = lastMid_ = lastHigh_ = 1e9f;
    low_.reset(); mid_.reset(); high_.reset();
    recalcIfChanged();
}

void ToneEq::recalcIfChanged() {
    const float lo = lowDb_.load(std::memory_order_relaxed);
    const float md = midDb_.load(std::memory_order_relaxed);
    const float hi = highDb_.load(std::memory_order_relaxed);
    if (lo != lastLow_)  { const float z1=low_.z1, z2=low_.z2;  low_  = Biquad::lowShelf (sr_, 100.0f, lo);       low_.z1=z1; low_.z2=z2;  lastLow_ = lo; }
    if (md != lastMid_)  { const float z1=mid_.z1, z2=mid_.z2;  mid_  = Biquad::peaking  (sr_, 700.0f, 0.7f, md);  mid_.z1=z1; mid_.z2=z2;  lastMid_ = md; }
    if (hi != lastHigh_) { const float z1=high_.z1,z2=high_.z2; high_ = Biquad::highShelf(sr_, 3200.0f, hi);      high_.z1=z1; high_.z2=z2; lastHigh_ = hi; }
}

void ToneEq::process(float* buf, int numSamples) {
    if (! enabled_.load(std::memory_order_relaxed)) return;   // passthrough
    recalcIfChanged();                                        // at most once per block
    for (int i = 0; i < numSamples; ++i) {
        float y = low_.processSample(buf[i]);
        y = mid_.processSample(y);
        y = high_.processSample(y);
        buf[i] = y;
    }
    // Anti-denormal flush: once per block, not per-sample (matches Gain/NoiseGate).
    low_.flushDenormals(); mid_.flushDenormals(); high_.flushDenormals();
}

} // namespace dsp
