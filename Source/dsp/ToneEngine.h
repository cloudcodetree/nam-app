#pragma once
#include <atomic>
#include <memory>
#include <vector>
#include "dsp/Gain.h"
#include "model/NamModel.h"

namespace dsp {
class ToneEngine {
public:
    void prepare(int sampleRate, int maxBlock);
    void setModel(std::shared_ptr<nam::NamModel> m);  // hand-off (see .cpp)
    void setInputDb(float db)  { inGain_.setDb(db); }
    void setOutputDb(float db) { outGain_.setDb(db); }
    // Real-time safe. Mono in -> mono out. May be called with model==null.
    // Contract: prepare(maxBlock) must be called before render(), and every
    // render() call must have numSamples <= maxBlock.
    void render(const float* in, float* out, int numSamples);
private:
    Gain inGain_, outGain_;

    // --- RT-safe model ownership contract ---------------------------------
    // The audio thread must never touch a std::shared_ptr's control block
    // (its ref-count inc/dec is not lock-free on all platforms, notably
    // libc++) and must never be the thread that runs the last decrement
    // (which would call ~NamModel / delete on the audio thread).
    //
    // - active_ is the ONLY member the audio thread reads. It is a plain
    //   atomic raw pointer load (genuinely lock-free everywhere).
    // - current_ and retired_ are owned and mutated exclusively by the
    //   control/message thread. current_ keeps the live model alive;
    //   retired_ holds previously-active models that render() may still
    //   have a raw pointer to.
    // - Reclamation (freeing retired_ entries) happens only in prepare(),
    //   which the caller guarantees runs while audio is stopped/not yet
    //   started -- i.e. no concurrent render() call -- so it is a safe
    //   point to drop the shared_ptrs and let the destructors run off the
    //   audio thread.
    std::atomic<nam::NamModel*> active_{nullptr};            // audio thread reads this only
    std::shared_ptr<nam::NamModel> current_;                 // control thread: keeps active alive
    std::vector<std::shared_ptr<nam::NamModel>> retired_;     // control thread: awaiting reclaim in prepare()

    int sampleRate_ = 48000;
    int maxBlock_   = 128;
    std::vector<float> scratch_;             // preallocated in prepare(); no audio-thread allocation
};
} // namespace dsp
