# NAM Player — Phase 3: Local Library (favorites, recents, offline)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A persistent local library of amp models (`.nam`) and cab IRs (`.wav`) the user manages offline — import files, mark favorites, see recents, and load any entry into the engine with one click. Everything works without a network (Phase 4 will populate this same library from TONE3000).

**Architecture:** Two JUCE-free, unit-tested units — `LibraryStore` (the persistent index) and `LibraryImporter` (copies a file into the library dir + extracts light metadata) — plus a JUCE library panel in `MainComponent`. The library is a directory the app owns (`.../NAM Player/library/{models,irs}`) with a `library.json` index. Reuses NeuralAudio's bundled `nlohmann::json` (no second copy) for the index and model-metadata parsing, and dr_wav (Phase 2) for IR metadata.

**Tech Stack:** C++17, JUCE 8.0.15, nlohmann::json (from NeuralAudio deps), dr_wav, Catch2 v3.

## Global Constraints

- **Language:** C++17.
- **JUCE isolation:** `Source/model/` (where `LibraryStore` and `LibraryImporter` live) MUST NOT include any JUCE header. Only `Source/app/` may.
- **Not on the audio thread:** the library is control/UI-side only. Loading an entry ultimately calls the existing `ToneEngine::setModel` / `setImpulse` RT-safe hand-offs — the library code itself never touches the audio thread.
- **Purity/testability:** `LibraryStore` takes its library directory as a constructor argument and takes timestamps as parameters (never reads the clock itself), so tests are deterministic and hermetic (temp dir + fixed timestamps).
- **nlohmann::json:** reuse NeuralAudio's copy. `NeuralModel.h` includes `"json.hpp"`, so the header dir `extern/NeuralAudio/deps/NeuralAmpModelerCore/Dependencies/nlohmann` is the include path; add it to `nam_tests` and `NamPlayer`, and `#include "json.hpp"` in the library `.cpp` files. Do NOT vendor a second nlohmann copy (ODR risk).
- **Robustness:** all library operations are best-effort and never throw across the public API — a missing/corrupt index loads as empty; a bad import returns nullptr; metadata extraction failure leaves fields blank but still imports the file.
- **Atomic index writes:** `save()` writes `library.json.tmp` then renames over `library.json` so a crash mid-write can't corrupt the index.

## File Structure

```
Source/model/
  LibraryEntry.h              # POD entry struct + LibraryType enum (JUCE-free)
  LibraryStore.h  .cpp        # persistent index: CRUD, favorites, recents, save/load
  LibraryImporter.h .cpp      # copy file into library dir + extract metadata + register
Source/app/
  LibraryPanel.h  .cpp        # JUCE ListBox UI for the library (models + IRs)
  MainComponent.h/.cpp        # MODIFIED: embed LibraryPanel, wire load/import/persist
tests/
  test_librarystore.cpp  test_libraryimporter.cpp
  CMakeLists.txt             # MODIFIED: new sources + nlohmann include dir
```

---

### Task 1: `LibraryEntry` + `LibraryStore` — the persistent index (JUCE-free)

The in-memory index of library entries with JSON persistence, favorites, and recents. Pure and deterministic (injected dir + timestamps).

**Files:**
- Create: `Source/model/LibraryEntry.h`
- Create: `Source/model/LibraryStore.h`
- Create: `Source/model/LibraryStore.cpp`
- Create: `tests/test_librarystore.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  namespace nam {
  enum class LibraryType { Model, Ir };

  struct LibraryEntry {
      std::string id;          // == fileName; unique within its type's subdir
      LibraryType type = LibraryType::Model;
      std::string displayName;
      std::string fileName;    // relative to the type subdir (models/ or irs/)
      bool        favorite = false;
      long long   addedAt = 0;      // epoch seconds (injected by caller)
      long long   lastUsedAt = 0;
      std::string arch;        // model architecture (blank for IRs / unknown)
      double      loudness = 0.0;   // model loudness dBFS (0 if unknown)
      int         frames = 0;       // IR length in samples (0 for models)
      int         sampleRate = 0;   // IR sample rate (0 for models)
  };

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

      std::vector<LibraryEntry> all(LibraryType) const;              // sorted by displayName (ci)
      std::vector<LibraryEntry> favorites(LibraryType) const;        // favorite==true, same sort
      std::vector<LibraryEntry> recents(LibraryType, int limit) const;// lastUsedAt desc, >0 only

      std::string dir() const;         // library root
      std::string subdir(LibraryType) const;   // models/ or irs/ absolute path
  };
  }
  ```

- [ ] **Step 1: Write LibraryEntry.h** (the struct + enum above, header-only, includes only `<string>`).

- [ ] **Step 2: Write LibraryStore.h** (the class declaration above; members: `std::string dir_;` `std::vector<LibraryEntry> entries_;`).

- [ ] **Step 3: Write the failing tests**

```cpp
// tests/test_librarystore.cpp
#include <catch2/catch_all.hpp>
#include <filesystem>
#include "model/LibraryStore.h"
using namespace nam;

static std::string tmpLib() {
    auto p = std::filesystem::temp_directory_path() /
             ("namlib_" + std::to_string(::testing_counter++));
    return p.string();
}
// simple unique counter (no rng/clock)
int testing_counter = 0;

static LibraryEntry model(const std::string& id, const std::string& name) {
    LibraryEntry e; e.id = id; e.type = LibraryType::Model;
    e.displayName = name; e.fileName = id; return e;
}

TEST_CASE("LibraryStore add/find/list round-trips") {
    LibraryStore s(tmpLib());
    s.add(model("a.nam", "Bravo"));
    s.add(model("b.nam", "Alpha"));
    REQUIRE(s.find("a.nam") != nullptr);
    auto all = s.all(LibraryType::Model);
    REQUIRE(all.size() == 2);
    REQUIRE(all[0].displayName == "Alpha");   // sorted case-insensitively
    REQUIRE(all[1].displayName == "Bravo");
}

TEST_CASE("LibraryStore persists across save/load") {
    auto dir = tmpLib();
    { LibraryStore s(dir); auto e = model("x.nam","X"); e.arch="SlimmableContainer"; e.loudness=-26.4; s.add(e); s.setFavorite("x.nam", true); REQUIRE(s.save()); }
    LibraryStore s2(dir);
    REQUIRE(s2.load());
    auto* e = s2.find("x.nam");
    REQUIRE(e != nullptr);
    REQUIRE(e->favorite == true);
    REQUIRE(e->arch == "SlimmableContainer");
    REQUIRE(e->loudness == Catch::Approx(-26.4));
}

TEST_CASE("LibraryStore favorites and recents") {
    LibraryStore s(tmpLib());
    s.add(model("a.nam","A")); s.add(model("b.nam","B")); s.add(model("c.nam","C"));
    s.setFavorite("b.nam", true);
    REQUIRE(s.favorites(LibraryType::Model).size() == 1);
    s.markUsed("a.nam", 100); s.markUsed("c.nam", 300); s.markUsed("b.nam", 200);
    auto r = s.recents(LibraryType::Model, 2);
    REQUIRE(r.size() == 2);
    REQUIRE(r[0].id == "c.nam");   // most recent first
    REQUIRE(r[1].id == "b.nam");
}

TEST_CASE("LibraryStore load with no index file is empty, not an error") {
    LibraryStore s(tmpLib());
    REQUIRE(s.load());                       // absent index -> ok
    REQUIRE(s.all(LibraryType::Model).empty());
}

TEST_CASE("LibraryStore remove deletes the file") {
    auto dir = tmpLib();
    LibraryStore s(dir);
    // create a fake model file in the models subdir
    auto path = std::filesystem::path(s.subdir(LibraryType::Model)) / "k.nam";
    std::ofstream(path) << "{}";
    s.add(model("k.nam","K"));
    REQUIRE(std::filesystem::exists(path));
    REQUIRE(s.remove("k.nam"));
    REQUIRE_FALSE(std::filesystem::exists(path));
    REQUIRE(s.find("k.nam") == nullptr);
}
```
NOTE: add `#include <fstream>` to the test. The `testing_counter` global is defined once in this TU.

- [ ] **Step 4: Write LibraryStore.cpp**

Implement with `std::filesystem` and nlohmann `"json.hpp"`. Key points:
- Constructor: store `dir_`; `std::filesystem::create_directories(dir_)`, `.../models`, `.../irs`.
- `subdir()`: `dir_/models` or `dir_/irs`. `entry.fileName` is relative to that.
- `load()`: if `dir_/library.json` doesn't exist → clear entries, return true. Else parse with a try/catch (on parse error → clear, return false). Deserialize each entry field with `value(key, default)` so missing fields are tolerated. Map `type` string "model"/"ir" ↔ enum.
- `save()`: build a json array from `entries_`, write to `library.json.tmp`, `std::filesystem::rename` to `library.json`. Return false on any filesystem_error (caught).
- `add()`: if an entry with the same id exists, replace it; else push_back. Return pointer to the stored entry.
- `remove()`: find by id; if found, `std::filesystem::remove(subdir(type)/fileName)` (ignore if missing), erase from vector, return true.
- `setFavorite`/`markUsed`: mutate the found entry, return true/false if found.
- `all()`: copy entries of the given type, sort by `displayName` case-insensitively (lowercase compare).
- `favorites()`: `all()` filtered to `favorite`.
- `recents()`: entries of type with `lastUsedAt > 0`, sorted by `lastUsedAt` desc, truncated to `limit`.
- Do not throw across any public method (wrap filesystem/json in try/catch where they can throw).

- [ ] **Step 5: Wire CMake, run tests (fail → pass)**

In `tests/CMakeLists.txt`: append `test_librarystore.cpp` and `${CMAKE_SOURCE_DIR}/Source/model/LibraryStore.cpp`; add the nlohmann include dir:
```cmake
target_include_directories(nam_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/extern/NeuralAudio/deps/NeuralAmpModelerCore/Dependencies/nlohmann)
```
Run: `cmake --build --preset default --target nam_tests -j4 && ./build/tests/nam_tests "LibraryStore*"`
Expected: PASS. Then full suite.

- [ ] **Step 6: Commit**

```bash
git add Source/model/LibraryEntry.h Source/model/LibraryStore.h Source/model/LibraryStore.cpp tests/test_librarystore.cpp tests/CMakeLists.txt
git commit -m "feat: LibraryStore persistent index with favorites and recents"
```

---

### Task 2: `LibraryImporter` — copy file into the library + extract metadata (JUCE-free)

Copies a source `.nam`/`.wav` into the store's `models/`/`irs/` dir under a unique name, extracts light metadata, and registers an entry.

**Files:**
- Create: `Source/model/LibraryImporter.h`
- Create: `Source/model/LibraryImporter.cpp`
- Create: `tests/test_libraryimporter.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `LibraryStore`, nlohmann (model metadata), dr_wav (IR metadata).
- Produces:
  ```cpp
  namespace nam {
  // Copies sourcePath into store.subdir(type) under a unique filename, extracts
  // best-effort metadata, adds an entry (addedAt = now), saves the index, and
  // returns the stored entry (or nullptr on failure, e.g. unreadable source).
  const LibraryEntry* importIntoLibrary(LibraryStore& store,
                                        const std::string& sourcePath,
                                        LibraryType type, long long now);
  }
  ```

- [ ] **Step 1: Write the failing tests**

```cpp
// tests/test_libraryimporter.cpp
#include <catch2/catch_all.hpp>
#include <filesystem>
#include <fstream>
#include "model/LibraryImporter.h"
using namespace nam;

extern int testing_counter;   // reuse the counter defined in test_librarystore.cpp
static std::string tmpLib2() {
    return (std::filesystem::temp_directory_path() /
            ("namlib_i_" + std::to_string(testing_counter++))).string();
}

TEST_CASE("importIntoLibrary copies a model and extracts architecture") {
    LibraryStore s(tmpLib2());
    auto* e = importIntoLibrary(s, NAM_MODEL_A2_REAL, LibraryType::Model, 1000);
    REQUIRE(e != nullptr);
    REQUIRE(e->type == LibraryType::Model);
    REQUIRE(e->arch == "SlimmableContainer");           // real A2 capture
    REQUIRE(std::filesystem::exists(std::filesystem::path(s.subdir(LibraryType::Model)) / e->fileName));
    REQUIRE(e->addedAt == 1000);
}

TEST_CASE("importIntoLibrary copies an IR and reads its frame count") {
    LibraryStore s(tmpLib2());
    // write a small mono 48k float WAV via dr_wav (helper mirrors test_irloader)
    auto wav = writeTestWav(std::vector<float>(2048, 0.1f), 48000);
    auto* e = importIntoLibrary(s, wav, LibraryType::Ir, 2000);
    std::filesystem::remove(wav);
    REQUIRE(e != nullptr);
    REQUIRE(e->type == LibraryType::Ir);
    REQUIRE(e->frames == 2048);
    REQUIRE(e->sampleRate == 48000);
}

TEST_CASE("importIntoLibrary returns nullptr on missing source") {
    LibraryStore s(tmpLib2());
    REQUIRE(importIntoLibrary(s, "nope.nam", LibraryType::Model, 3000) == nullptr);
}

TEST_CASE("importIntoLibrary makes filenames unique on collision") {
    LibraryStore s(tmpLib2());
    auto* a = importIntoLibrary(s, NAM_MODEL_A2_REAL, LibraryType::Model, 1);
    auto* b = importIntoLibrary(s, NAM_MODEL_A2_REAL, LibraryType::Model, 2);
    REQUIRE(a != nullptr); REQUIRE(b != nullptr);
    REQUIRE(a->fileName != b->fileName);   // second copy gets a distinct name
}
```
Provide a `writeTestWav(samples, sr)` helper in this test file (same dr_wav write approach as `tests/test_irloader.cpp`, WITHOUT re-defining `DR_WAV_IMPLEMENTATION` — it is already defined in `IrLoader.cpp`'s TU; just include `"dr_wav.h"` and call `drwav_*`). The test target already has the dr_wav include dir from Phase 2.

- [ ] **Step 2: Write LibraryImporter.h** (the single free-function declaration above; includes `LibraryStore.h`).

- [ ] **Step 3: Write LibraryImporter.cpp**

- `#include "json.hpp"`, `#include "dr_wav.h"` (no implementation macro), `<filesystem>`, `<fstream>`.
- Guard: if `!std::filesystem::exists(sourcePath)` → return nullptr.
- Compute a unique destination filename in `store.subdir(type)`: start from the source filename; while a file of that name exists in the subdir, insert ` (n)` before the extension.
- Copy the file (`std::filesystem::copy_file`, catch errors → nullptr).
- Build the entry: `id = fileName = destName`, `displayName` = (for models) the `metadata.name` from the `.nam` JSON if present, else the filename stem; `addedAt = now`.
- Metadata (best-effort, each in its own try/catch; failure leaves fields default):
  - Model: parse the copied `.nam` with nlohmann; `arch = j.value("architecture", "")`; `loudness = j["metadata"].value("loudness", 0.0)` (guard presence).
  - IR: `drwav_init_file` the copied wav; read `channels`/`totalPCMFrameCount`/`sampleRate`; `frames = (int) totalPCMFrameCount`, `sampleRate`; `drwav_uninit`.
- `store.add(entry)` then `store.save()`; return the stored pointer (`store.find(id)`).

- [ ] **Step 4: Wire CMake, run tests (fail → pass)**

Append `test_libraryimporter.cpp` and `${CMAKE_SOURCE_DIR}/Source/model/LibraryImporter.cpp` to `nam_tests`. `NAM_MODEL_A2_REAL` is already defined (Phase 1). Run: `cmake --build --preset default --target nam_tests -j4 && ./build/tests/nam_tests "importIntoLibrary*"` then full suite. Also ASan.

- [ ] **Step 5: Commit**

```bash
git add Source/model/LibraryImporter.h Source/model/LibraryImporter.cpp tests/test_libraryimporter.cpp tests/CMakeLists.txt
git commit -m "feat: LibraryImporter copies files into library and extracts metadata"
```

---

### Task 3: `LibraryPanel` + MainComponent integration — the library UI

A JUCE panel listing the library (models + IRs) with favorites, recents-first ordering, click-to-load, and "Add to library" wired from the existing file choosers. Persists across launches.

**Files:**
- Create: `Source/app/LibraryPanel.h`
- Create: `Source/app/LibraryPanel.cpp`
- Modify: `Source/app/MainComponent.h`
- Modify: `Source/app/MainComponent.cpp`
- Modify: `CMakeLists.txt` (add LibraryPanel.cpp + LibraryStore.cpp + LibraryImporter.cpp + nlohmann include dir to NamPlayer)

**Interfaces:**
- Consumes: `nam::LibraryStore`, `nam::importIntoLibrary`, `nam::LibraryEntry`.
- Produces: `class LibraryPanel : public juce::Component` with:
  ```cpp
  std::function<void(const nam::LibraryEntry&)> onLoadEntry;  // user clicked an entry
  void refresh();   // re-read from the store and repaint the lists
  ```

- [ ] **Step 1: Write LibraryPanel.h/.cpp**

A `juce::Component` holding two `juce::ListBox`es (Models, IRs) backed by `juce::ListBoxModel`s that read `store.all(type)` (with favorites shown first — sort: favorite desc, then displayName). Each row shows a star toggle (favorite) + name; double-click or a "Load" affordance fires `onLoadEntry(entry)`. A `LibraryStore&` is injected by reference. `refresh()` re-queries the store and calls `updateContent()` on both lists. Keep it focused; use `paintListBoxItem` for rows and a click on the left ~20px to toggle favorite (calls `store.setFavorite` + `store.save()` + refresh).

- [ ] **Step 2: Embed in MainComponent.h**

Add members: `nam::LibraryStore library_;` (constructed with the app library dir — see Step 3), `LibraryPanel libraryPanel_ { library_ };`, and a helper `void libraryDirInit();`. Initialize `library_` with the path from `juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("NAM Player/library").getFullPathName().toStdString()`.

- [ ] **Step 3: Wire MainComponent.cpp**

- In the constructor: `library_.load();` then `addAndMakeVisible(libraryPanel_); libraryPanel_.refresh();`.
- `libraryPanel_.onLoadEntry = [this](const nam::LibraryEntry& e){ ... }`: if `e.type == Model`, load it through the existing model path (reuse the code that calls `host_.requestLoad(absolutePath, ...)` then `engine_.setModel`); if `Ir`, call `nam::loadImpulseResponse(absolutePath, deviceSR, dsp::kMaxIrTaps)` + `engine_.setImpulse` and enable the cab. Compute `absolutePath = library_.subdir(e.type) + "/" + e.fileName`. Then `library_.markUsed(e.id, nowSeconds()); library_.save(); libraryPanel_.refresh();` where `nowSeconds()` uses `std::chrono::system_clock` (app layer — allowed).
- Add "Add to Library" buttons (or extend the existing choosers): after a successful model/IR file selection in `loadButtonClicked`/`loadIrClicked`, also call `nam::importIntoLibrary(library_, path, type, nowSeconds())` and `libraryPanel_.refresh()`. (Import copies into the library; loading still works from the original path for the immediate audition.)
- Grow the window and lay out `libraryPanel_` in `resized()` (e.g., a right-hand column or a section below the chain controls).

- [ ] **Step 4: CMake + build both presets**

Add to `CMakeLists.txt` NamPlayer: sources `Source/app/LibraryPanel.cpp`, `Source/model/LibraryStore.cpp`, `Source/model/LibraryImporter.cpp`; and the nlohmann include dir. Run `cmake --build --preset default --target NamPlayer -j4` and `--preset debug`. Both must link. Run the full test suite (no regression).

- [ ] **Step 5: Commit**

```bash
git add Source/app/LibraryPanel.h Source/app/LibraryPanel.cpp Source/app/MainComponent.h Source/app/MainComponent.cpp CMakeLists.txt
git commit -m "feat: library panel UI with favorites, recents, click-to-load, and import"
```

---

### Task 4: Manual verification + finish

- [ ] **Step 1: Full suite regression**

Run: `cmake --build --preset default --target nam_tests -j4 && ./build/tests/nam_tests` — all PASS.

- [ ] **Step 2: Manual checks**

Launch the app, then confirm:
1. Import `models/A2.nam` (and a cab `.wav`) via "Add to Library" — they appear in the library lists.
2. Click a library entry → it loads and plays.
3. Star a favorite → it sorts to the top and survives an app restart.
4. Recently-used entries show in recents order after loading a few.
5. Quit and relaunch → the library persists (index + copied files in `.../NAM Player/library`).

- [ ] **Step 3: Empty commit marking verification**

```bash
git commit --allow-empty -m "chore: Phase 3 local library verified"
```

---

## Self-Review (completed)

- **Spec coverage (§10.3):** persistent library of downloaded/imported models + IRs (Tasks 1–2), favorites + recents + offline (Task 1), browse/load UI + import (Task 3), verification (Task 4). ✅
- **JUCE isolation:** `LibraryStore`, `LibraryImporter`, `LibraryEntry` are JUCE-free and unit-tested; only `LibraryPanel`/`MainComponent` use JUCE. ✅
- **Purity/determinism:** store takes injected dir + timestamps → hermetic tests (temp dirs, fixed times), no clock/rng in the tested core. ✅
- **Dependency hygiene:** reuse NeuralAudio's nlohmann (no second copy → no ODR risk); dr_wav implementation stays single-TU (IrLoader.cpp). ✅
- **Placeholder scan:** full code/steps given for the JUCE-free core; the UI task is structural (ListBox glue, manually verified) consistent with prior phases' UI tasks. ✅
- **Type consistency:** `LibraryEntry`, `LibraryType`, `importIntoLibrary(...)`, and `LibraryStore` method names are consistent across Tasks 1–3. ✅
- **Deferred (out of scope):** in-app deletion UI beyond basic remove, tags/search, dedup by content hash, and TONE3000 download-into-library (that's Phase 4, which reuses `importIntoLibrary`/`LibraryStore`).
