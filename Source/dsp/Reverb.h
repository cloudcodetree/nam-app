#pragma once
#include <atomic>
#include <vector>

namespace dsp {
// Freeverb-style mono reverb: 8 lowpass-feedback comb filters in parallel,
// summed, then 4 allpass filters in series. Header declares the node and the
// two filter primitives; prepare()/process() live in Reverb.cpp.
//
// JUCE-free, RT-safe: prepare() sizes every comb/allpass buffer to the tuning
// tables; process() never allocates, locks, or throws. All recursive state
// flushes denormals so a decaying tail can't drop into denormal CPU spikes.
class Reverb {
public:
    void prepare(int sampleRate, int /*maxBlock*/);

    void setEnabled(bool on)  { enabled_.store(on, std::memory_order_relaxed); }
    void setRoomSize(float r) { roomSize_.store(r, std::memory_order_relaxed); } // 0..1
    void setDamping(float d)  { damping_.store(d, std::memory_order_relaxed); }  // 0..1
    void setMix(float m)      { mix_.store(m, std::memory_order_relaxed); }      // 0..1 wet

    void process(const float* in, float* out, int numSamples); // in-place safe

private:
    static constexpr int kNumCombs     = 8;
    static constexpr int kNumAllpasses = 4;

    // Lowpass-feedback comb: the feedback path is one-pole low-passed, so each
    // successive echo is duller (this is what "damping" controls).
    struct Comb {
        std::vector<float> buf;
        int   pos   = 0;
        float store = 0.0f; // one-pole lowpass state
        void reset() { for (float& v : buf) v = 0.0f; pos = 0; store = 0.0f; }
        float process(float input, float feedback, float damp1, float damp2) {
            const int n = (int) buf.size();
            float output = buf[(size_t) pos];
            if (output < 1.0e-15f && output > -1.0e-15f) output = 0.0f; // flush denormal
            store = output * damp2 + store * damp1;
            if (store < 1.0e-15f && store > -1.0e-15f) store = 0.0f;    // flush denormal
            buf[(size_t) pos] = input + store * feedback;
            if (++pos >= n) pos = 0;
            return output;
        }
    };

    // Schroeder allpass: fixed 0.5 feedback, smears phase to thicken density.
    struct Allpass {
        std::vector<float> buf;
        int pos = 0;
        void reset() { for (float& v : buf) v = 0.0f; pos = 0; }
        float process(float input) {
            const int n = (int) buf.size();
            float bufout = buf[(size_t) pos];
            if (bufout < 1.0e-15f && bufout > -1.0e-15f) bufout = 0.0f; // flush denormal
            const float output = -input + bufout;
            buf[(size_t) pos] = input + bufout * 0.5f;
            if (++pos >= n) pos = 0;
            return output;
        }
    };

    std::atomic<bool>  enabled_{false};
    std::atomic<float> roomSize_{0.5f};
    std::atomic<float> damping_{0.5f};
    std::atomic<float> mix_{0.3f};

    Comb    combs_[kNumCombs];
    Allpass allpasses_[kNumAllpasses];
    bool    prepared_ = false;
};
} // namespace dsp
