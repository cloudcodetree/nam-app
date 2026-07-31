#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#include "model/IrLoader.h"
#include <algorithm>

namespace nam {

std::shared_ptr<const std::vector<float>>
loadImpulseResponse(const std::string& path, int targetSampleRate, int maxTaps) {
    unsigned int channels = 0, sampleRate = 0;
    drwav_uint64 frameCount = 0;
    float* raw = drwav_open_file_and_read_pcm_frames_f32(
        path.c_str(), &channels, &sampleRate, &frameCount, nullptr);
    if (raw == nullptr || channels == 0 || frameCount == 0) {
        if (raw) drwav_free(raw, nullptr);
        return nullptr;
    }
    // Downmix to mono.
    std::vector<float> mono((size_t) frameCount, 0.0f);
    for (drwav_uint64 i = 0; i < frameCount; ++i) {
        float acc = 0.0f;
        for (unsigned int c = 0; c < channels; ++c) acc += raw[i * channels + c];
        mono[(size_t) i] = acc / (float) channels;
    }
    drwav_free(raw, nullptr);

    // Linear-resample to target rate if needed.
    if ((int) sampleRate != targetSampleRate && sampleRate > 0) {
        const double ratio = (double) targetSampleRate / (double) sampleRate;
        const size_t outN = (size_t) (mono.size() * ratio);
        std::vector<float> rs(outN, 0.0f);
        for (size_t i = 0; i < outN; ++i) {
            const double srcPos = i / ratio;
            const size_t i0 = (size_t) srcPos;
            const double frac = srcPos - i0;
            const float a = mono[std::min(i0, mono.size() - 1)];
            const float b = mono[std::min(i0 + 1, mono.size() - 1)];
            rs[i] = (float) (a + (b - a) * frac);
        }
        mono.swap(rs);
    }

    if ((int) mono.size() > maxTaps) mono.resize((size_t) maxTaps);
    return std::make_shared<const std::vector<float>>(std::move(mono));
}

} // namespace nam
