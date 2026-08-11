#pragma once
#include <string>

namespace nam {

enum class LibraryType { Model, Ir };

struct LibraryEntry {
    std::string id;   // == fileName; unique within its type's subdir
    LibraryType type = LibraryType::Model;
    std::string displayName;
    std::string fileName;   // relative to the type subdir (models/ or irs/)
    bool favorite = false;
    long long addedAt = 0;   // epoch seconds (injected by caller)
    long long lastUsedAt = 0;
    std::string arch;        // model architecture (blank for IRs / unknown)
    double loudness = 0.0;   // model loudness dBFS (0 if unknown)
    int frames = 0;          // IR length in samples (0 for models)
    int sampleRate = 0;      // IR sample rate (0 for models)
};

}   // namespace nam
