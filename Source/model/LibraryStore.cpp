#include "model/LibraryStore.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

#include "json.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace nam {

namespace {

std::string toLower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                    [](unsigned char c) { return std::tolower(c); });
    return out;
}

std::string typeToString(LibraryType t) {
    return t == LibraryType::Model ? "model" : "ir";
}

LibraryType typeFromString(const std::string& s) {
    return s == "ir" ? LibraryType::Ir : LibraryType::Model;
}

json entryToJson(const LibraryEntry& e) {
    json j;
    j["id"] = e.id;
    j["type"] = typeToString(e.type);
    j["displayName"] = e.displayName;
    j["fileName"] = e.fileName;
    j["favorite"] = e.favorite;
    j["addedAt"] = e.addedAt;
    j["lastUsedAt"] = e.lastUsedAt;
    j["arch"] = e.arch;
    j["loudness"] = e.loudness;
    j["frames"] = e.frames;
    j["sampleRate"] = e.sampleRate;
    return j;
}

LibraryEntry entryFromJson(const json& j) {
    LibraryEntry e;
    e.id = j.value("id", std::string());
    e.type = typeFromString(j.value("type", std::string("model")));
    e.displayName = j.value("displayName", std::string());
    e.fileName = j.value("fileName", std::string());
    e.favorite = j.value("favorite", false);
    e.addedAt = j.value("addedAt", (long long)0);
    e.lastUsedAt = j.value("lastUsedAt", (long long)0);
    e.arch = j.value("arch", std::string());
    e.loudness = j.value("loudness", 0.0);
    e.frames = j.value("frames", 0);
    e.sampleRate = j.value("sampleRate", 0);
    return e;
}

} // namespace

LibraryStore::LibraryStore(std::string libraryDir) : dir_(std::move(libraryDir)) {
    try {
        fs::create_directories(dir_);
        fs::create_directories(fs::path(dir_) / "models");
        fs::create_directories(fs::path(dir_) / "irs");
    } catch (const std::exception&) {
        // Best-effort directory creation; failures surface later as
        // load()/save() failures rather than a thrown exception here.
    }
}

std::string LibraryStore::dir() const { return dir_; }

std::string LibraryStore::subdir(LibraryType t) const {
    return (fs::path(dir_) / (t == LibraryType::Model ? "models" : "irs")).string();
}

bool LibraryStore::load() {
    try {
        fs::path indexPath = fs::path(dir_) / "library.json";
        if (!fs::exists(indexPath)) {
            entries_.clear();
            return true;
        }
        std::ifstream in(indexPath);
        if (!in) {
            entries_.clear();
            return false;
        }
        json j;
        in >> j;
        if (!j.is_array()) {
            entries_.clear();
            return false;
        }
        std::list<LibraryEntry> loaded;
        for (const auto& item : j) {
            loaded.push_back(entryFromJson(item));
        }
        entries_ = std::move(loaded);
        return true;
    } catch (const std::exception&) {
        entries_.clear();
        return false;
    }
}

bool LibraryStore::save() const {
    try {
        json arr = json::array();
        for (const auto& e : entries_) {
            arr.push_back(entryToJson(e));
        }
        fs::path tmpPath = fs::path(dir_) / "library.json.tmp";
        fs::path finalPath = fs::path(dir_) / "library.json";
        bool writeOk = false;
        {
            std::ofstream out(tmpPath);
            if (!out) return false;
            out << arr.dump(2);
            out.flush();
            writeOk = out.good();
        }
        if (!writeOk) {
            std::error_code ec;
            fs::remove(tmpPath, ec);
            return false;
        }
        fs::rename(tmpPath, finalPath);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

const LibraryEntry* LibraryStore::add(const LibraryEntry& e) {
    for (auto& existing : entries_) {
        if (existing.id == e.id) {
            existing = e;
            return &existing;
        }
    }
    entries_.push_back(e);
    return &entries_.back();
}

bool LibraryStore::remove(const std::string& id) {
    auto it = std::find_if(entries_.begin(), entries_.end(),
                            [&](const LibraryEntry& e) { return e.id == id; });
    if (it == entries_.end()) return false;

    try {
        fs::path filePath = fs::path(subdir(it->type)) / it->fileName;
        std::error_code ec;
        fs::remove(filePath, ec);
    } catch (const std::exception&) {
        // Ignore filesystem errors deleting the underlying file; the
        // entry is still removed from the index below.
    }

    entries_.erase(it);
    return true;
}

bool LibraryStore::setFavorite(const std::string& id, bool fav) {
    auto it = std::find_if(entries_.begin(), entries_.end(),
                            [&](const LibraryEntry& e) { return e.id == id; });
    if (it == entries_.end()) return false;
    it->favorite = fav;
    return true;
}

bool LibraryStore::markUsed(const std::string& id, long long now) {
    auto it = std::find_if(entries_.begin(), entries_.end(),
                            [&](const LibraryEntry& e) { return e.id == id; });
    if (it == entries_.end()) return false;
    it->lastUsedAt = now;
    return true;
}

const LibraryEntry* LibraryStore::find(const std::string& id) const {
    auto it = std::find_if(entries_.begin(), entries_.end(),
                            [&](const LibraryEntry& e) { return e.id == id; });
    return it == entries_.end() ? nullptr : &(*it);
}

std::vector<LibraryEntry> LibraryStore::all(LibraryType t) const {
    std::vector<LibraryEntry> result;
    for (const auto& e : entries_) {
        if (e.type == t) result.push_back(e);
    }
    std::sort(result.begin(), result.end(), [](const LibraryEntry& a, const LibraryEntry& b) {
        return toLower(a.displayName) < toLower(b.displayName);
    });
    return result;
}

std::vector<LibraryEntry> LibraryStore::favorites(LibraryType t) const {
    auto result = all(t);
    result.erase(std::remove_if(result.begin(), result.end(),
                                 [](const LibraryEntry& e) { return !e.favorite; }),
                 result.end());
    return result;
}

std::vector<LibraryEntry> LibraryStore::recents(LibraryType t, int limit) const {
    std::vector<LibraryEntry> result;
    if (limit <= 0) return result;
    for (const auto& e : entries_) {
        if (e.type == t && e.lastUsedAt > 0) result.push_back(e);
    }
    std::sort(result.begin(), result.end(), [](const LibraryEntry& a, const LibraryEntry& b) {
        return a.lastUsedAt > b.lastUsedAt;
    });
    if (static_cast<size_t>(limit) < result.size()) {
        result.resize(static_cast<size_t>(limit));
    }
    return result;
}

} // namespace nam
