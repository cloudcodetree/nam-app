#include "dsp/ToneEngine.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>

namespace dsp {

void ToneEngine::prepare(int sampleRate, int maxBlock) {
    sampleRate_ = sampleRate;
    maxBlock_ = maxBlock;
    inGain_.reset(1.0f, sampleRate);
    outGain_.reset(1.0f, sampleRate);
    normGain_.reset(1.0f, sampleRate);
    if (current_)   // re-apply normalisation for the model that stays loaded
        normGain_.setDb(current_->recommendedOutputDbAdjustment());
    scratch_.assign(maxBlock, 0.0f);

    // Safe point: audio is stopped/not yet started here, so no render() can
    // be concurrently reading a retired raw pointer. Dropping these
    // shared_ptrs may run ~NamModel, but that happens on the control thread.
    retired_.clear();

    gate_.prepare(sampleRate);
    irCab_.prepare(sampleRate, maxBlock);
    eq_.prepare(sampleRate);
    delay_.prepare(sampleRate, maxBlock);
    reverb_.prepare(sampleRate, maxBlock);
}

void ToneEngine::setModel(std::shared_ptr<nam::NamModel> m) {
    // Control/message thread only. Keep the previously-active model alive
    // in retired_ (a concurrently-running render() may still hold its raw
    // pointer) rather than freeing it here. Actual reclamation happens in
    // prepare(), at a safe point. Publish the new pointer with a release
    // store; render() acquires it, giving a genuinely lock-free hand-off.
    // Block-gated reclamation: a retiree is freeable once at least two
    // render blocks have COMPLETED since it was retired — any in-flight
    // render that could still hold its raw pointer has provably finished
    // (renders are serialized on the audio thread). A plain count-based
    // trim would be unsafe: several setModel calls can flush in one
    // message-loop drain with no block boundary between them. This keeps
    // hot-swapped models from pinning memory until the next prepare().
    const auto nowBlocks = blockCount_.load(std::memory_order_acquire);
    retired_.erase(std::remove_if(retired_.begin(), retired_.end(),
                                  [nowBlocks](const auto& r) { return nowBlocks >= r.second + 2; }),
                   retired_.end());
    // Stagnation fallback for a STOPPED device (the counter never advances,
    // so the block gate can never fire): freeing a zero-completed-blocks
    // retiree is only safe when no render is in flight RIGHT NOW — the
    // inRender_ acquire proves the last render fully finished, and any
    // render starting after this check reads the current active_ pointer,
    // never a retiree. A counter compare alone shows completion, not
    // absence (twelve swaps can drain while one render is mid-block).
    while (retired_.size() > 8 && nowBlocks == retired_.front().second &&
           !inRender_.load(std::memory_order_acquire))
        retired_.erase(retired_.begin());
    if (current_) retired_.push_back({ std::move(current_), nowBlocks });
    current_ = m;

    // NAM loudness normalisation: land every model near the -18 dBFS
    // reference so hot captures don't clip and quiet ones aren't buried.
    normGain_.setDb(m ? m->recommendedOutputDbAdjustment() : 0.0f);

    active_.store(m ? m.get() : nullptr, std::memory_order_release);
}

void ToneEngine::render(const float* in, float* out, int numSamples) {
    inRender_.store(true, std::memory_order_relaxed);
    // RT-safe timing: a steady_clock read is a non-blocking, allocation-free
    // counter read (mach_absolute_time on macOS). Used only to report load.
    const auto t0 = std::chrono::steady_clock::now();

    nam::NamModel* m = active_.load(std::memory_order_acquire);
    if (m) {
        // Signal order: input gain -> gate -> model -> IR cab -> EQ ->
        //               delay -> reverb -> output gain.
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
        const int n = std::min({ numSamples, (int)scratch_.size(), m->maxBlock() });

        // Telemetry: a block we had to clamp is a real problem signal
        // (the active model couldn't take the whole buffer) -- track it.
        if (n < numSamples) overCap_.fetch_add(1, std::memory_order_relaxed);

        // Signal order: input gain -> gate -> model -> IR cab -> EQ ->
        //               delay -> reverb -> output gain.
        for (int i = 0; i < n; ++i) scratch_[i] = inGain_.applyNext(in[i]);
        gate_.process(scratch_.data(), scratch_.data(), n);   // gate before amp
        m->process(scratch_.data(), out, n);                  // amp
        for (int i = 0; i < n; ++i)
            out[i] = normGain_.applyNext(out[i]);   // loudness normalisation
        irCab_.process(out, out, n);                // cab
        eq_.process(out, n);                        // tone
        delay_.process(out, out, n);                // time FX: delay
        reverb_.process(out, out, n);               // time FX: reverb
        for (int i = 0; i < n; ++i) out[i] = outGain_.applyNext(out[i]);
        for (int i = n; i < numSamples; ++i) out[i] = 0.0f;
    } else {
        // No model: in-gain -> gate -> IR cab -> EQ -> delay -> reverb ->
        // out-gain. Reads `in`/writes `out` directly (not scratch_-backed),
        // so it is not bounds-limited here.
        for (int i = 0; i < numSamples; ++i) out[i] = inGain_.applyNext(in[i]);
        gate_.process(out, out, numSamples);
        irCab_.process(out, out, numSamples);
        eq_.process(out, numSamples);
        delay_.process(out, out, numSamples);
        reverb_.process(out, out, numSamples);
        for (int i = 0; i < numSamples; ++i) out[i] = outGain_.applyNext(out[i]);
    }

    // --- Lock-free telemetry tail (still on the audio thread, RT-safe) ------
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i) peak = std::max(peak, std::fabs(out[i]));
    outPeak_.store(peak, std::memory_order_relaxed);

    const auto t1 = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(t1 - t0).count();
    const double period = sampleRate_ > 0 ? (double)numSamples / sampleRate_ : 0.0;
    if (period > 0.0) cpuLoad_.store((float)(elapsed / period), std::memory_order_relaxed);
    // RELEASE: setModel's acquire load of blockCount_ must observe every
    // memory operation of this render before reclaiming a retired model —
    // the block-gated free is only safe with this happens-before edge.
    blockCount_.fetch_add(1, std::memory_order_release);
    // RELEASE: a control-thread acquire that observes false knows every
    // memory op of this render (including its model reads) is finished.
    inRender_.store(false, std::memory_order_release);
}

}   // namespace dsp
