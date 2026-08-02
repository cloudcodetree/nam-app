#pragma once
#include <string>
#include <vector>
#include "model/LibraryEntry.h"

namespace nam {

// JUCE-free persistent index of library entries (models + IRs) with
// favorites, recents, and JSON persistence. Deterministic: the library
// directory and all timestamps are injected by the caller; this class
// never touches the clock. No public method throws.
class LibraryStore {
public:
    explicit LibraryStore(std::string libraryDir);  // ensures dir + models/ + irs/ exist

    bool load();                     // read library.json; true if loaded or absent (empty)
    bool save() const;               // atomic write of library.json

    const LibraryEntry* add(const LibraryEntry& e);  // insert/replace by id; returns stored
    bool remove(const std::string& id);              // removes entry AND deletes its file
    bool setFavorite(const std::string& id, bool fav);
    bool markUsed(const std::string& id, long long now);
    const LibraryEntry* find(const std::string& id) const;

    std::vector<LibraryEntry> all(LibraryType) const;               // sorted by displayName (ci)
    std::vector<LibraryEntry> favorites(LibraryType) const;         // favorite==true, same sort
    std::vector<LibraryEntry> recents(LibraryType, int limit) const;// lastUsedAt desc, >0 only

    std::string dir() const;                 // library root
    std::string subdir(LibraryType) const;   // models/ or irs/ absolute path

private:
    std::string dir_;
    std::vector<LibraryEntry> entries_;
};

}
