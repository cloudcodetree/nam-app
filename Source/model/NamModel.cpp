#include "model/NamModel.h"
#include <NeuralAudio/NeuralModel.h>
#include <cmath>

using NeuralAudio::NeuralModel;
using NeuralAudio::NeuralModelLoader;

namespace nam {

std::unique_ptr<NamModel> NamModel::load(const std::string& path,
                                         int sampleRate, int maxBlock) {
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
