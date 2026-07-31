#include "model/NamModel.h"
#include <NeuralAudio/NeuralModel.h>
#include <cmath>
#include <exception>

using NeuralAudio::NeuralModel;
using NeuralAudio::NeuralModelLoader;

namespace nam {

std::unique_ptr<NamModel> NamModel::load(const std::string& path,
                                         int sampleRate, int maxBlock) {
    // Honor the documented "returns nullptr on failure" contract: a malformed
    // (but existing) file can make NeuralAudio's loader throw (e.g. nlohmann::json
    // parse errors on bad content). Catch any such exception and report failure
    // rather than letting it propagate and/or leak a partially constructed model.
    try {
        NeuralModelLoader loader;
        loader.SetExternalSampleRate(sampleRate);
        NeuralModel* raw = loader.CreateFromFile(path);
        if (raw == nullptr) return nullptr;
        raw->SetMaxAudioBufferSize(maxBlock);

        std::unique_ptr<NamModel> m(new NamModel());
        m->model_      = raw;
        m->sampleRate_ = sampleRate;
        m->inputDb_    = raw->GetRecommendedInputDBAdjustment();
        m->outputDb_   = raw->GetRecommendedOutputDBAdjustment();
        return m;
    } catch (const std::exception&) {
        return nullptr;
    } catch (...) {
        return nullptr;
    }
}

NamModel::~NamModel() {
    delete static_cast<NeuralModel*>(model_);
}

void NamModel::process(const float* input, float* output, int numSamples) {
    // NeuralAudio::Process takes non-const float*; it does not mutate input.
    static_cast<NeuralModel*>(model_)->Process(
        const_cast<float*>(input), output, static_cast<size_t>(numSamples));
}

} // namespace nam
