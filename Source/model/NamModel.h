#pragma once
#include <memory>
#include <string>

namespace nam {
class NamModel {
public:
    // Loads a model baked for `sampleRate` with capacity `maxBlock`.
    // Returns nullptr on failure (bad path / unsupported model).
    static std::unique_ptr<NamModel> load(const std::string& path,
                                          int sampleRate, int maxBlock);
    NamModel(const NamModel&) = delete;
    NamModel& operator=(const NamModel&) = delete;
    ~NamModel();

    void  process(const float* input, float* output, int numSamples);
    float recommendedInputDb()  const { return inputDb_; }
    float recommendedOutputDb() const { return outputDb_; }
    int   sampleRate() const { return sampleRate_; }

private:
    NamModel() = default;
    void* model_ = nullptr;   // NeuralAudio::NeuralModel* (opaque to keep header JUCE/dep-free)
    int   sampleRate_ = 0;
    float inputDb_  = 0.0f;
    float outputDb_ = 0.0f;
};
} // namespace nam
