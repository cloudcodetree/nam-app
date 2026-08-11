#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>
#include "dsp/Gain.h"
#include "dsp/NoiseGate.h"
#include "dsp/IrCab.h"
#include "dsp/ToneEq.h"
#include "dsp/Delay.h"
#include "dsp/Reverb.h"
#include "model/NamModel.h"

namespace dsp {
class ToneEngine {
public:
    void prepare(int sampleRate, int maxBlock);
    void setModel(std::shared_ptr<nam::NamModel> m);   // hand-off (see .cpp)
    void setInputDb(float db) { inGain_.setDb(db); }
    void setOutputDb(float db) { outGain_.setDb(db); }

    // Signal-chain stage controls (forward to the nodes).
    void setGateEnabled(bool on) { gate_.setEnabled(on); }
    void setGateThresholdDb(float db) { gate_.setThresholdDb(db); }
    void setIrEnabled(bool on) { irCab_.setEnabled(on); }
    void setImpulse(std::shared_ptr<const std::vector<float>> ir) {
        irCab_.setImpulse(std::move(ir));
    }
    void setEqEnabled(bool on) { eq_.setEnabled(on); }
    void setLowDb(float db) { eq_.setLowDb(db); }
    void setMidDb(float db) { eq_.setMidDb(db); }
    void setHighDb(float db) { eq_.setHighDb(db); }
    void setDelayEnabled(bool on) { delay_.setEnabled(on); }
    void setDelayTimeMs(float ms) { delay_.setTimeMs(ms); }
    void setDelayFeedback(float f) { delay_.setFeedback(f); }
    void setDelayMix(float m) { delay_.setMix(m); }
    void setReverbEnabled(bool on) { reverb_.setEnabled(on); }
    void setReverbRoomSize(float r) { reverb_.setRoomSize(r); }
    void setReverbDamping(float d) { reverb_.setDamping(d); }
    void setReverbMix(float m) { reverb_.setMix(m); }

    // Real-time safe. Mono in -> mono out. May be called with model==null.
    // Contract: prepare(maxBlock) must be called before render(), and every
    // render() call must have numSamples <= maxBlock.
    void render(const float* in, float* out, int numSamples);

    // --- Lock-free debug/telemetry -----------------------------------------
    // Written by the audio thread (relaxed atomics), read by the UI thread.
    // This is how you "watch" the real-time path without ever stopping it:
    // no breakpoint, no lock, no logging on the callback.
    float outputPeak() const {
        return outPeak_.load(std::memory_order_relaxed);
    }   // linear, last block
    float cpuLoad() const {
        return cpuLoad_.load(std::memory_order_relaxed);
    }   // render time / block period, 0..1+
    uint32_t overCapacityCount() const {
        return overCap_.load(std::memory_order_relaxed);
    }   // blocks fed > model capacity (xrun-ish)
    uint64_t blockCount() const {
        return blockCount_.load(std::memory_order_relaxed);
    }   // total render() calls
private:
    Gain inGain_, outGain_, normGain_;   // normGain_: NAM loudness normalisation
    NoiseGate gate_;
    IrCab irCab_;
    ToneEq eq_;
    Delay delay_;
    Reverb reverb_;

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
    std::atomic<nam::NamModel*> active_{ nullptr };   // audio thread reads this only
    std::shared_ptr<nam::NamModel> current_;          // control thread: keeps active alive
    std::vector<std::shared_ptr<nam::NamModel>>
        retired_;   // control thread: awaiting reclaim in prepare()

    int sampleRate_ = 48000;
    int maxBlock_ = 128;
    std::vector<float> scratch_;   // preallocated in prepare(); no audio-thread allocation

    // Telemetry (audio thread writes, UI thread reads). See getters above.
    std::atomic<float> outPeak_{ 0.0f };
    std::atomic<float> cpuLoad_{ 0.0f };
    std::atomic<uint32_t> overCap_{ 0 };
    std::atomic<uint64_t> blockCount_{ 0 };
};
}   // namespace dsp
