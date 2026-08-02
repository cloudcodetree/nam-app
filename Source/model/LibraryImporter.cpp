#include "model/LibraryImporter.h"

#include <filesystem>
#include <fstream>

#include "dr_wav.h"
#include "json.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace nam {

namespace {

// Returns a filename (not a full path) that does not already exist in dir,
// starting from the given base filename and inserting " (2)", " (3)", ...
// before the extension until unique.
std::string uniqueFileName(const std::string& dir, const std::string& baseFileName) {
    fs::path base(baseFileName);
    std::string stem = base.stem().string();
    std::string ext = base.extension().string();

    std::string candidate = baseFileName;
    int n = 2;
    while (fs::exists(fs::path(dir) / candidate)) {
        candidate = stem + " (" + std::to_string(n) + ")" + ext;
        ++n;
    }
    return candidate;
}

// Parses the copied .nam JSON for arch/loudness, and (if present) a
// metadata.name to use as the display name. Best-effort: on any failure,
// e's fields are left at whatever the caller already set.
void extractModelMetadata(const std::string& path, LibraryEntry& e) {
    try {
        std::ifstream in(path);
        if (!in) return;
        json j;
        in >> j;
        e.arch = j.value("architecture", std::string());
        if (j.contains("metadata") && j["metadata"].is_object()) {
            const auto& meta = j["metadata"];
            e.loudness = meta.value("loudness", 0.0);
            if (meta.contains("name") && meta["name"].is_string()) {
                e.displayName = meta["name"].get<std::string>();
            }
        }
    } catch (const std::exception&) {
        // Best-effort: leave fields at their defaults.
    }
}

void extractIrMetadata(const std::string& path, LibraryEntry& e) {
    try {
        drwav wav;
        if (!drwav_init_file(&wav, path.c_str(), nullptr)) return;
        e.frames = (int) wav.totalPCMFrameCount;
        e.sampleRate = (int) wav.sampleRate;
        drwav_uninit(&wav);
    } catch (const std::exception&) {
        // Best-effort: leave fields at their defaults.
    }
}

} // namespace

const LibraryEntry* importIntoLibrary(LibraryStore& store, const std::string& sourcePath,
                                       LibraryType type, long long now) {
    try {
        if (!fs::exists(sourcePath)) return nullptr;

        const std::string destDir = store.subdir(type);
        const std::string srcFileName = fs::path(sourcePath).filename().string();
        const std::string destName = uniqueFileName(destDir, srcFileName);
        const fs::path destPath = fs::path(destDir) / destName;

        std::error_code ec;
        if (!fs::copy_file(sourcePath, destPath, fs::copy_options::none, ec) || ec) {
            return nullptr;
        }

        LibraryEntry entry;
        entry.id = destName;
        entry.fileName = destName;
        entry.type = type;
        entry.addedAt = now;
        entry.displayName = fs::path(destName).stem().string();

        if (type == LibraryType::Model) {
            extractModelMetadata(destPath.string(), entry);
        } else {
            extractIrMetadata(destPath.string(), entry);
        }

        store.add(entry);
        store.save();
        return store.find(entry.id);
    } catch (const std::exception&) {
        return nullptr;
    }
}

} // namespace nam
