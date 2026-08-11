#pragma once
#include <string>
#include "model/LibraryStore.h"

namespace nam {

// Copies sourcePath into store.subdir(type) under a unique filename, extracts
// best-effort metadata, adds an entry (addedAt = now), saves the index, and
// returns the stored entry (or nullptr on failure, e.g. unreadable source).
// A non-empty displayName replaces the filename-stem fallback (IR WAVs carry
// no name metadata, so cache stems like "ir_79857" would otherwise show in
// the UI); a model's own metadata.name still wins when present. Never throws.
const LibraryEntry* importIntoLibrary(LibraryStore& store, const std::string& sourcePath,
                                       LibraryType type, long long now,
                                       const std::string& displayName = {});

}
