#include "dsp/IrCab.h"
#include <algorithm>

namespace dsp {

void IrCab::prepare(int /*sampleRate*/, int /*maxBlock*/) {
    ring_.assign(kMaxIrTaps, 0.0f);
    writePos_ = 0;
    retired_.clear();   // safe point: audio stopped
}

void IrCab::setImpulse(std::shared_ptr<const std::vector<float>> ir) {
    if (current_) retired_.push_back(std::move(current_));
    current_ = std::move(ir);
    active_.store(current_ ? current_.get() : nullptr, std::memory_order_release);
}

void IrCab::process(const float* in, float* out, int numSamples) {
    const std::vector<float>* ir = enabled_.load(std::memory_order_relaxed)
                                       ? active_.load(std::memory_order_acquire)
                                       : nullptr;
    if (ir == nullptr || ir->empty()) {   // passthrough
        if (in != out)
            for (int i = 0; i < numSamples; ++i) out[i] = in[i];
        return;
    }
    const int taps = std::min((int)ir->size(), kMaxIrTaps);
    const float* h = ir->data();
    const int cap = (int)ring_.size();
    for (int n = 0; n < numSamples; ++n) {
        ring_[writePos_] = in[n];
        float acc = 0.0f;
        int idx = writePos_;
        for (int k = 0; k < taps; ++k) {
            acc += h[k] * ring_[idx];
            idx = idx == 0 ? cap - 1 : idx - 1;
        }
        out[n] = acc;
        writePos_ = writePos_ == cap - 1 ? 0 : writePos_ + 1;
    }
}

}   // namespace dsp
