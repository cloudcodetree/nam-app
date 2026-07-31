#include "dsp/ToneEngine.h"
#include <atomic>
#include <cassert>

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
    nam::NamModel* m = active_.load(std::memory_order_acquire);
    if (m) {
        // Signal order: input gain -> model -> output gain.
        // scratch_ is preallocated in prepare() so this stays
        // allocation-free on the audio thread. Contract: numSamples
        // must be <= the maxBlock passed to prepare(); guard defensively
        // against a caller violating that contract so we never overflow
        // scratch_ (heap-overflow found by review).
        assert(numSamples <= (int) scratch_.size());
        const int n = numSamples <= (int) scratch_.size() ? numSamples : (int) scratch_.size();

        for (int i = 0; i < n; ++i)
            scratch_[i] = inGain_.applyNext(in[i]);
        m->process(scratch_.data(), out, n);
        for (int i = 0; i < n; ++i)
            out[i] = outGain_.applyNext(out[i]);
        for (int i = n; i < numSamples; ++i)
            out[i] = 0.0f;
        return;
    }
    // No model: in-gain -> passthrough -> out-gain. Reads `in`/writes `out`
    // directly (not scratch_-backed), so it is not bounds-limited here.
    for (int i = 0; i < numSamples; ++i)
        out[i] = outGain_.applyNext(inGain_.applyNext(in[i]));
}

} // namespace dsp
