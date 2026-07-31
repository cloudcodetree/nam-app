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
    std::shared_ptr<nam::NamModel> model_;   // read on audio thread
    std::atomic<bool> hasModel_{false};
    int sampleRate_ = 48000;
    int maxBlock_   = 128;
    std::vector<float> scratch_;             // preallocated in prepare(); no audio-thread allocation
};
} // namespace dsp
