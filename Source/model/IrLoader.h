#pragma once
#include <memory>
#include <string>
#include <vector>

namespace nam {
// Loads a cab IR .wav, downmixes to mono, linear-resamples to
// targetSampleRate if the file's rate differs, and truncates to maxTaps.
// Returns nullptr on any failure (bad path, unreadable file, empty audio).
// Never throws.
std::shared_ptr<const std::vector<float>> loadImpulseResponse(const std::string& path,
                                                              int targetSampleRate, int maxTaps);
}   // namespace nam
