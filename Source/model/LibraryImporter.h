#pragma once
#include <string>
#include "model/LibraryStore.h"

namespace nam {

// Copies sourcePath into store.subdir(type) under a unique filename, extracts
// best-effort metadata, adds an entry (addedAt = now), saves the index, and
// returns the stored entry (or nullptr on failure, e.g. unreadable source).
// Never throws.
const LibraryEntry* importIntoLibrary(LibraryStore& store, const std::string& sourcePath,
                                       LibraryType type, long long now);

}
