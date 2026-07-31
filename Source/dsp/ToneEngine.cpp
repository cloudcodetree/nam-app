#include "dsp/ToneEngine.h"
#include <atomic>

namespace dsp {

void ToneEngine::prepare(int sampleRate, int maxBlock) {
    sampleRate_ = sampleRate;
    maxBlock_   = maxBlock;
    inGain_.reset(1.0f, sampleRate);
    outGain_.reset(1.0f, sampleRate);
    scratch_.assign(maxBlock, 0.0f);
}

void ToneEngine::setModel(std::shared_ptr<nam::NamModel> m) {
    // Publish the new model; std::atomic_store on shared_ptr is the lock-free
    // hand-off. The audio thread loads it via atomic_load in render().
    std::atomic_store(&model_, m);
    hasModel_.store(m != nullptr, std::memory_order_release);
}

void ToneEngine::render(const float* in, float* out, int numSamples) {
    if (hasModel_.load(std::memory_order_acquire)) {
        std::shared_ptr<nam::NamModel> m = std::atomic_load(&model_);
        if (m) {
            // Signal order: input gain -> model -> output gain.
            // scratch_ is preallocated in prepare() so this stays
            // allocation-free on the audio thread. Contract: numSamples
            // must be <= the maxBlock passed to prepare().
            for (int i = 0; i < numSamples; ++i)
                scratch_[i] = inGain_.applyNext(in[i]);
            m->process(scratch_.data(), out, numSamples);
            for (int i = 0; i < numSamples; ++i)
                out[i] = outGain_.applyNext(out[i]);
            return;
        }
    }
    // No model: in-gain -> passthrough -> out-gain.
    for (int i = 0; i < numSamples; ++i)
        out[i] = outGain_.applyNext(inGain_.applyNext(in[i]));
}

} // namespace dsp
