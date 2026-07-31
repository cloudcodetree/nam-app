#include "dsp/ToneEngine.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>

namespace dsp {

void ToneEngine::prepare(int sampleRate, int maxBlock) {
    sampleRate_ = sampleRate;
    maxBlock_   = maxBlock;
    inGain_.reset(1.0f, sampleRate);
    outGain_.reset(1.0f, sampleRate);
    scratch_.assign(maxBlock, 0.0f);

    // Safe point: audio is stopped/not yet started here, so no render() can
    // be concurrently reading a retired raw pointer. Dropping these
    // shared_ptrs may run ~NamModel, but that happens on the control thread.
    retired_.clear();

    gate_.prepare(sampleRate);
    irCab_.prepare(sampleRate, maxBlock);
    eq_.prepare(sampleRate);
}

void ToneEngine::setModel(std::shared_ptr<nam::NamModel> m) {
    // Control/message thread only. Keep the previously-active model alive
    // in retired_ (a concurrently-running render() may still hold its raw
    // pointer) rather than freeing it here. Actual reclamation happens in
    // prepare(), at a safe point. Publish the new pointer with a release
    // store; render() acquires it, giving a genuinely lock-free hand-off.
    if (current_)
        retired_.push_back(std::move(current_));
    current_ = m;
    active_.store(m ? m.get() : nullptr, std::memory_order_release);
}

void ToneEngine::render(const float* in, float* out, int numSamples) {
    // RT-safe timing: a steady_clock read is a non-blocking, allocation-free
    // counter read (mach_absolute_time on macOS). Used only to report load.
    const auto t0 = std::chrono::steady_clock::now();

    nam::NamModel* m = active_.load(std::memory_order_acquire);
    if (m) {
        // Signal order: input gain -> gate -> model -> IR cab -> EQ -> output gain.
        // scratch_ is preallocated in prepare() so this stays
        // allocation-free on the audio thread. Contract: numSamples
        // must be <= the maxBlock passed to prepare(); guard defensively
        // against a caller violating that contract so we never overflow
        // scratch_ (heap-overflow found by review).
        //
        // Additionally clamp to the ACTIVE MODEL's own prepared max block:
        // prepare() can resize scratch_ larger (e.g. device buffer size
        // increased) before the async model reload completes, leaving an
        // old model - still prepared/asserting on a smaller maxBlock -
        // active. NAM-core (non-internal) models do not chunk internally
        // and will write out of bounds if fed more than their prepared
        // capacity, so scratch_'s size alone is not a sufficient bound.
        const int n = std::min({numSamples, (int) scratch_.size(), m->maxBlock()});

        // Telemetry: a block we had to clamp is a real problem signal
        // (the active model couldn't take the whole buffer) -- track it.
        if (n < numSamples)
            overCap_.fetch_add(1, std::memory_order_relaxed);

        // Signal order: input gain -> gate -> model -> IR cab -> EQ -> output gain.
        for (int i = 0; i < n; ++i)
            scratch_[i] = inGain_.applyNext(in[i]);
        gate_.process(scratch_.data(), scratch_.data(), n);   // gate before amp
        m->process(scratch_.data(), out, n);                  // amp
        irCab_.process(out, out, n);                          // cab
        eq_.process(out, n);                                  // tone
        for (int i = 0; i < n; ++i)
            out[i] = outGain_.applyNext(out[i]);
        for (int i = n; i < numSamples; ++i)
            out[i] = 0.0f;
    } else {
        // No model: in-gain -> gate -> IR cab -> EQ -> out-gain. Reads `in`/
        // writes `out` directly (not scratch_-backed), so it is not
        // bounds-limited here.
        for (int i = 0; i < numSamples; ++i)
            out[i] = inGain_.applyNext(in[i]);
        gate_.process(out, out, numSamples);
        irCab_.process(out, out, numSamples);
        eq_.process(out, numSamples);
        for (int i = 0; i < numSamples; ++i)
            out[i] = outGain_.applyNext(out[i]);
    }

    // --- Lock-free telemetry tail (still on the audio thread, RT-safe) ------
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        peak = std::max(peak, std::fabs(out[i]));
    outPeak_.store(peak, std::memory_order_relaxed);

    const auto t1 = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(t1 - t0).count();
    const double period  = sampleRate_ > 0 ? (double) numSamples / sampleRate_ : 0.0;
    if (period > 0.0)
        cpuLoad_.store((float) (elapsed / period), std::memory_order_relaxed);
    blockCount_.fetch_add(1, std::memory_order_relaxed);
}

} // namespace dsp
