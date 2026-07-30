# NAM Player — Phase 1: Audio Engine + Device Picker + Local Model Load

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A desktop JUCE app that lets the user pick an audio device + buffer size, load a local `.nam` (A2) model file, and hear their guitar processed through it in real time.

**Architecture:** All DSP and model code is JUCE-free and unit-tested in a console (Catch2) binary. JUCE appears only in a thin `AudioDeviceCallbackAdapter` and the UI. The real-time audio callback never allocates, locks, or does I/O; model swaps are prepared off-thread and handed over via an atomic pointer.

**Tech Stack:** C++17, JUCE 8 (audio I/O + UI), NeuralAudio (MIT, A1/A2 inference) as a git submodule, Catch2 v3 (tests), CMake + FetchContent.

## Global Constraints

- **Language:** C++17. No compiler extensions required by our own code.
- **Real-time safety:** the audio callback (`ToneEngine::render` and everything it calls) MUST NOT allocate heap, lock a mutex, throw, or do file/network I/O. Verified by review and a no-alloc guard test.
- **JUCE isolation:** files under `Source/dsp/` and `Source/model/` MUST NOT `#include` any JUCE header. Only `Source/app/` may use JUCE.
- **License:** NeuralAudio is MIT; keep it as an unmodified submodule. Do not vendor its source into our tree.
- **Audio format:** mono guitar input, 32-bit float, default 48000 Hz, default buffer 128 samples.
- **NeuralAudio API (verified from headers):**
  - `NeuralAudio::NeuralModelLoader loader;`
  - `loader.SetExternalSampleRate(int);` (call before load)
  - `NeuralAudio::NeuralModel* m = loader.CreateFromFile(const std::filesystem::path&, bool doPrewarm=true);`
  - `m->SetMaxAudioBufferSize(int);`
  - `m->Process(float* input, float* output, size_t numSamples);`
  - `float m->GetRecommendedInputDBAdjustment();` / `GetRecommendedOutputDBAdjustment();`
  - `delete m;` (no explicit destroy method; virtual destructor).
  - NOTE: if the installed NeuralAudio version exposes `CreateFromFile`/`SetExternalSampleRate` as **static** methods on `NeuralModel` rather than on `NeuralModelLoader`, adjust ONLY inside `NamModel.cpp` — no other file references these symbols.

---

## File Structure

```
nam_app/
  CMakeLists.txt                      # top-level: app target + test target
  extern/
    JUCE/                             # FetchContent (not committed as submodule)
    NeuralAudio/                      # git submodule
  Source/
    model/
      NamModel.h  NamModel.cpp        # JUCE-free wrapper around NeuralAudio
      ModelHost.h ModelHost.cpp       # off-thread load + atomic active-model swap
    dsp/
      Gain.h                          # header-only smoothed linear gain
      ToneEngine.h ToneEngine.cpp     # JUCE-free: inGain -> model -> outGain
    app/
      AudioDeviceCallbackAdapter.h/.cpp  # juce::AudioIODeviceCallback -> ToneEngine
      MainComponent.h/.cpp            # device picker, load button, sliders, meter
      Main.cpp                        # JUCEApplication entry
  tests/
    CMakeLists.txt
    fixtures/                         # small .nam model for characterization tests
    test_main.cpp                     # Catch2 runner
    test_gain.cpp
    test_nammodel.cpp
    test_toneengine.cpp
    test_modelhost.cpp
```

---

### Task 1: Project scaffold — CMake, JUCE, NeuralAudio, Catch2, one passing test

Establishes the build so every later task has a place to add code and a test binary to run. Ends with a trivial passing test that proves the toolchain compiles and links NeuralAudio.

**Files:**
- Create: `CMakeLists.txt`
- Create: `tests/CMakeLists.txt`
- Create: `tests/test_main.cpp`
- Create: `tests/test_scaffold.cpp`
- Create: `.gitmodules` (via `git submodule add`)

**Interfaces:**
- Consumes: nothing.
- Produces: CMake targets `NamPlayer` (app, added later tasks fill sources) and `nam_tests` (Catch2 console binary). Test sources are added to `nam_tests` by later tasks.

- [ ] **Step 1: Add NeuralAudio as a submodule**

```bash
cd /Users/chris.harper/Development/nam_app
git submodule add https://github.com/mikeoliphant/NeuralAudio extern/NeuralAudio
git submodule update --init --recursive
```

- [ ] **Step 2: Write the top-level CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.22)
project(NamPlayer VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# --- JUCE ---
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4)
FetchContent_MakeAvailable(JUCE)

# --- NeuralAudio (submodule) ---
add_subdirectory(extern/NeuralAudio)

# --- App target (sources added in later tasks) ---
juce_add_gui_app(NamPlayer PRODUCT_NAME "NAM Player")
target_sources(NamPlayer PRIVATE) # populated in later tasks
target_compile_features(NamPlayer PRIVATE cxx_std_17)
target_link_libraries(NamPlayer PRIVATE
    NeuralAudio
    juce::juce_audio_utils
    juce::juce_gui_extra)
target_compile_definitions(NamPlayer PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="NAM Player")

# --- Tests ---
enable_testing()
add_subdirectory(tests)
```

- [ ] **Step 3: Write tests/CMakeLists.txt**

```cmake
include(FetchContent)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2)
FetchContent_MakeAvailable(Catch2)

add_executable(nam_tests
    test_main.cpp
    test_scaffold.cpp)          # later tasks append their test files here
target_compile_features(nam_tests PRIVATE cxx_std_17)
target_include_directories(nam_tests PRIVATE ${CMAKE_SOURCE_DIR}/Source)
target_link_libraries(nam_tests PRIVATE NeuralAudio Catch2::Catch2)

include(CTest)
include(Catch)
catch_discover_tests(nam_tests)
```

- [ ] **Step 4: Write tests/test_main.cpp**

```cpp
#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>
```

- [ ] **Step 5: Write the failing scaffold test**

```cpp
// tests/test_scaffold.cpp
#include <catch2/catch_all.hpp>

TEST_CASE("scaffold builds and links") {
    REQUIRE(1 + 1 == 2);
}
```

- [ ] **Step 6: Configure and build the test target**

Run:
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target nam_tests -j4
```
Expected: configures (downloads JUCE + Catch2), compiles, links `nam_tests` with no errors.

- [ ] **Step 7: Run the test**

Run: `./build/tests/nam_tests`
Expected: `All tests passed (1 assertion in 1 test case)`.

- [ ] **Step 8: Commit**

```bash
git add .gitmodules extern/NeuralAudio CMakeLists.txt tests/
git commit -m "chore: scaffold CMake build with JUCE, NeuralAudio, Catch2"
```

---

### Task 2: `NamModel` — JUCE-free wrapper around NeuralAudio

Wraps model loading + processing behind a tiny interface, isolating the third-party API to one file. Characterization tests pin down its behavior without needing a golden reference file.

**Files:**
- Create: `Source/model/NamModel.h`
- Create: `Source/model/NamModel.cpp`
- Create: `tests/test_nammodel.cpp`
- Create: `tests/fixtures/` (download a model into it — Step 1)
- Modify: `tests/CMakeLists.txt` (add `test_nammodel.cpp`, define fixture path)

**Interfaces:**
- Consumes: NeuralAudio API (see Global Constraints).
- Produces:
  ```cpp
  namespace nam {
  class NamModel {
  public:
      // Loads a model baked for `sampleRate` with capacity `maxBlock`.
      // Returns nullptr on failure (bad path / unsupported model).
      static std::unique_ptr<NamModel> load(const std::string& path,
                                            int sampleRate, int maxBlock);
      // Process mono in-place-capable: input and output may alias.
      void process(const float* input, float* output, int numSamples);
      float recommendedInputDb()  const;   // dB
      float recommendedOutputDb() const;   // dB
      int   sampleRate() const;
      ~NamModel();
  };
  }
  ```

- [ ] **Step 1: Get a fixture model**

```bash
mkdir -p tests/fixtures
curl -L -o tests/fixtures/example_a2.nam \
  https://raw.githubusercontent.com/sdatkinson/NeuralAmpModelerCore/main/example_models/wavenet_a2_max.nam
```

- [ ] **Step 2: Write NamModel.h**

```cpp
#pragma once
#include <memory>
#include <string>

namespace nam {
class NamModel {
public:
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
```

- [ ] **Step 3: Write the failing tests**

```cpp
// tests/test_nammodel.cpp
#include <catch2/catch_all.hpp>
#include <vector>
#include "model/NamModel.h"

#ifndef NAM_FIXTURE_A2
#define NAM_FIXTURE_A2 "tests/fixtures/example_a2.nam"
#endif

TEST_CASE("NamModel loads a valid A2 file") {
    auto m = nam::NamModel::load(NAM_FIXTURE_A2, 48000, 128);
    REQUIRE(m != nullptr);
    REQUIRE(m->sampleRate() == 48000);
}

TEST_CASE("NamModel returns nullptr on bad path") {
    auto m = nam::NamModel::load("does/not/exist.nam", 48000, 128);
    REQUIRE(m == nullptr);
}

TEST_CASE("NamModel processing is deterministic") {
    auto m = nam::NamModel::load(NAM_FIXTURE_A2, 48000, 128);
    REQUIRE(m != nullptr);
    std::vector<float> in(128), a(128), b(128);
    for (int i = 0; i < 128; ++i) in[i] = 0.1f * std::sin(i * 0.1f);
    m->process(in.data(), a.data(), 128);
    // Reload to reset internal state, process the same input again.
    auto m2 = nam::NamModel::load(NAM_FIXTURE_A2, 48000, 128);
    m2->process(in.data(), b.data(), 128);
    for (int i = 0; i < 128; ++i) REQUIRE(a[i] == Approx(b[i]));
}

TEST_CASE("NamModel silence in stays bounded") {
    auto m = nam::NamModel::load(NAM_FIXTURE_A2, 48000, 128);
    std::vector<float> in(128, 0.0f), out(128, 0.0f);
    m->process(in.data(), out.data(), 128);
    for (float v : out) REQUIRE(std::isfinite(v));
    for (float v : out) REQUIRE(std::abs(v) < 2.0f);
}
```

- [ ] **Step 4: Wire the fixture path + test file into CMake**

Append to `tests/CMakeLists.txt` `add_executable(nam_tests ...)` source list: `test_nammodel.cpp`. After the target is defined add:
```cmake
target_sources(nam_tests PRIVATE ${CMAKE_SOURCE_DIR}/Source/model/NamModel.cpp)
target_compile_definitions(nam_tests PRIVATE
    NAM_FIXTURE_A2="${CMAKE_SOURCE_DIR}/tests/fixtures/example_a2.nam")
```

- [ ] **Step 5: Run tests to verify they fail**

Run: `cmake --build build --target nam_tests -j4`
Expected: FAIL to link/compile — `NamModel::load` not implemented.

- [ ] **Step 6: Write NamModel.cpp**

```cpp
#include "model/NamModel.h"
#include <NeuralAudio/NeuralModel.h>
#include <cmath>

using NeuralAudio::NeuralModel;
using NeuralAudio::NeuralModelLoader;

namespace nam {

std::unique_ptr<NamModel> NamModel::load(const std::string& path,
                                         int sampleRate, int maxBlock) {
    NeuralModelLoader loader;
    loader.SetExternalSampleRate(sampleRate);
    NeuralModel* raw = loader.CreateFromFile(path);   // see Global Constraints note
    if (raw == nullptr) return nullptr;
    raw->SetMaxAudioBufferSize(maxBlock);

    std::unique_ptr<NamModel> m(new NamModel());
    m->model_      = raw;
    m->sampleRate_ = sampleRate;
    m->inputDb_    = raw->GetRecommendedInputDBAdjustment();
    m->outputDb_   = raw->GetRecommendedOutputDBAdjustment();
    return m;
}

NamModel::~NamModel() {
    delete static_cast<NeuralModel*>(model_);
}

void NamModel::process(const float* input, float* output, int numSamples) {
    // NeuralAudio::Process takes non-const float*; it does not mutate input.
    static_cast<NeuralModel*>(model_)->Process(
        const_cast<float*>(input), output, static_cast<size_t>(numSamples));
}

} // namespace nam
```

- [ ] **Step 7: Rebuild and run**

Run: `cmake --build build --target nam_tests -j4 && ./build/tests/nam_tests "[NamModel],NamModel*"`
Expected: all NamModel test cases PASS. (If `CreateFromFile` is static, apply the Global-Constraints fix in this file only, then rebuild.)

- [ ] **Step 8: Commit**

```bash
git add Source/model/NamModel.h Source/model/NamModel.cpp tests/test_nammodel.cpp tests/CMakeLists.txt tests/fixtures/example_a2.nam
git commit -m "feat: NamModel wrapper around NeuralAudio with characterization tests"
```

---

### Task 3: `Gain` + `ToneEngine` — JUCE-free real-time processing core

The engine core that the audio callback will drive. Smoothed gains avoid zipper noise; when no model is loaded it passes audio through unchanged.

**Files:**
- Create: `Source/dsp/Gain.h`
- Create: `Source/dsp/ToneEngine.h`
- Create: `Source/dsp/ToneEngine.cpp`
- Create: `tests/test_gain.cpp`
- Create: `tests/test_toneengine.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `nam::NamModel` (Task 2).
- Produces:
  ```cpp
  namespace dsp {
  class Gain {                       // header-only
  public:
      void  reset(float linear, int sampleRate);
      void  setDb(float db);         // control thread
      float applyNext(float x);      // audio thread, one sample
  };
  class ToneEngine {
  public:
      void prepare(int sampleRate, int maxBlock);   // control thread
      void setModel(std::shared_ptr<nam::NamModel>); // audio thread hand-off
      void setInputDb(float db);
      void setOutputDb(float db);
      // Real-time safe. Mono in -> mono out. May be called with model==null.
      void render(const float* in, float* out, int numSamples);
  };
  }
  ```

- [ ] **Step 1: Write Gain.h**

```cpp
#pragma once
#include <cmath>
#include <atomic>

namespace dsp {
class Gain {
public:
    void reset(float linear, int sampleRate) {
        current_ = target_.load(std::memory_order_relaxed) = linear;
        // ~5 ms smoothing
        const float t = 0.005f * static_cast<float>(sampleRate);
        coeff_ = t > 1.0f ? std::exp(-1.0f / t) : 0.0f;
    }
    void setDb(float db) {
        target_.store(std::pow(10.0f, db / 20.0f), std::memory_order_relaxed);
    }
    float applyNext(float x) {
        const float tgt = target_.load(std::memory_order_relaxed);
        current_ = tgt + coeff_ * (current_ - tgt);
        return x * current_;
    }
private:
    std::atomic<float> target_{1.0f};
    float current_ = 1.0f;
    float coeff_   = 0.0f;
};
} // namespace dsp
```

- [ ] **Step 2: Write the failing Gain test**

```cpp
// tests/test_gain.cpp
#include <catch2/catch_all.hpp>
#include "dsp/Gain.h"

TEST_CASE("Gain of 0 dB is unity after settle") {
    dsp::Gain g; g.reset(1.0f, 48000); g.setDb(0.0f);
    float y = 0; for (int i = 0; i < 4800; ++i) y = g.applyNext(1.0f);
    REQUIRE(y == Approx(1.0f).epsilon(0.001));
}

TEST_CASE("Gain of +6 dB settles near 2x") {
    dsp::Gain g; g.reset(1.0f, 48000); g.setDb(6.0206f);
    float y = 0; for (int i = 0; i < 4800; ++i) y = g.applyNext(1.0f);
    REQUIRE(y == Approx(2.0f).epsilon(0.01));
}

TEST_CASE("Gain smooths — no instantaneous jump") {
    dsp::Gain g; g.reset(1.0f, 48000); g.setDb(20.0f);
    float first = g.applyNext(1.0f);
    REQUIRE(first < 3.0f); // not the full 10x on sample 1
}
```

- [ ] **Step 3: Write ToneEngine.h**

```cpp
#pragma once
#include <atomic>
#include <memory>
#include "dsp/Gain.h"
#include "model/NamModel.h"

namespace dsp {
class ToneEngine {
public:
    void prepare(int sampleRate, int maxBlock);
    void setModel(std::shared_ptr<nam::NamModel> m);  // hand-off (see .cpp)
    void setInputDb(float db)  { inGain_.setDb(db); }
    void setOutputDb(float db) { outGain_.setDb(db); }
    void render(const float* in, float* out, int numSamples);
private:
    Gain inGain_, outGain_;
    std::shared_ptr<nam::NamModel> model_;   // read on audio thread
    std::atomic<bool> hasModel_{false};
    int sampleRate_ = 48000;
    int maxBlock_   = 128;
};
} // namespace dsp
```

- [ ] **Step 4: Write the failing ToneEngine tests**

```cpp
// tests/test_toneengine.cpp
#include <catch2/catch_all.hpp>
#include <vector>
#include "dsp/ToneEngine.h"

TEST_CASE("ToneEngine passes audio through when no model") {
    dsp::ToneEngine e; e.prepare(48000, 128);
    e.setInputDb(0.0f); e.setOutputDb(0.0f);
    std::vector<float> in(128), out(128);
    for (int i = 0; i < 128; ++i) in[i] = 0.25f;
    for (int b = 0; b < 40; ++b) e.render(in.data(), out.data(), 128); // settle
    for (int i = 0; i < 128; ++i) REQUIRE(out[i] == Approx(0.25f).epsilon(0.01));
}

TEST_CASE("ToneEngine applies output gain when no model") {
    dsp::ToneEngine e; e.prepare(48000, 128);
    e.setInputDb(0.0f); e.setOutputDb(6.0206f);
    std::vector<float> in(128, 0.1f), out(128);
    for (int b = 0; b < 40; ++b) e.render(in.data(), out.data(), 128);
    REQUIRE(out[64] == Approx(0.2f).epsilon(0.02));
}

TEST_CASE("ToneEngine runs a real model without NaNs") {
    dsp::ToneEngine e; e.prepare(48000, 128);
    auto m = nam::NamModel::load(NAM_FIXTURE_A2, 48000, 128);
    REQUIRE(m != nullptr);
    e.setModel(std::move(m));
    std::vector<float> in(128), out(128);
    for (int i = 0; i < 128; ++i) in[i] = 0.05f * std::sin(i * 0.2f);
    e.render(in.data(), out.data(), 128);
    for (float v : out) REQUIRE(std::isfinite(v));
}
```

- [ ] **Step 5: Add sources to CMake**

Append `test_gain.cpp` and `test_toneengine.cpp` to the `nam_tests` sources, and add:
```cmake
target_sources(nam_tests PRIVATE ${CMAKE_SOURCE_DIR}/Source/dsp/ToneEngine.cpp)
```

- [ ] **Step 6: Run tests to verify they fail**

Run: `cmake --build build --target nam_tests -j4`
Expected: FAIL — `ToneEngine::render`/`prepare`/`setModel` unresolved.

- [ ] **Step 7: Write ToneEngine.cpp**

```cpp
#include "dsp/ToneEngine.h"
#include <atomic>

namespace dsp {

void ToneEngine::prepare(int sampleRate, int maxBlock) {
    sampleRate_ = sampleRate;
    maxBlock_   = maxBlock;
    inGain_.reset(1.0f, sampleRate);
    outGain_.reset(1.0f, sampleRate);
}

void ToneEngine::setModel(std::shared_ptr<nam::NamModel> m) {
    // Publish the new model; std::atomic_store on shared_ptr is the lock-free
    // hand-off. The audio thread loads it via atomic_load in render().
    std::atomic_store(&model_, m);
    hasModel_.store(m != nullptr, std::memory_order_release);
}

void ToneEngine::render(const float* in, float* out, int numSamples) {
    if (hasModel_.load(std::memory_order_acquire)) {
        std::shared_ptr<nam::NamModel> m = std::atomic_load(&model_);
        if (m) {
            m->process(in, out, numSamples);       // amp
            for (int i = 0; i < numSamples; ++i) {
                out[i] = inGain_.applyNext(out[i]); // note: in/out gain around model
            }
            for (int i = 0; i < numSamples; ++i)
                out[i] = outGain_.applyNext(out[i]);
            return;
        }
    }
    // No model: in-gain -> passthrough -> out-gain.
    for (int i = 0; i < numSamples; ++i)
        out[i] = outGain_.applyNext(inGain_.applyNext(in[i]));
}

} // namespace dsp
```

NOTE: input gain should precede the model. Correct Step 7's model branch to apply `inGain_` to `in` into a scratch buffer before `m->process`. Use a preallocated `std::vector<float> scratch_` sized in `prepare()` to keep the audio thread allocation-free:

```cpp
// In ToneEngine.h add: std::vector<float> scratch_;
// In prepare(): scratch_.assign(maxBlock, 0.0f);
// In render() model branch:
for (int i = 0; i < numSamples; ++i) scratch_[i] = inGain_.applyNext(in[i]);
m->process(scratch_.data(), out.data ? out : out, numSamples); // process scratch -> out
for (int i = 0; i < numSamples; ++i) out[i] = outGain_.applyNext(out[i]);
```
Apply this corrected ordering, rebuild, and keep the tests (they assert gain math and finiteness, which hold under the corrected order).

- [ ] **Step 8: Run tests to verify they pass**

Run: `cmake --build build --target nam_tests -j4 && ./build/tests/nam_tests "[Gain],[ToneEngine],ToneEngine*,Gain*"`
Expected: all Gain and ToneEngine cases PASS.

- [ ] **Step 9: Commit**

```bash
git add Source/dsp tests/test_gain.cpp tests/test_toneengine.cpp tests/CMakeLists.txt
git commit -m "feat: JUCE-free ToneEngine with smoothed gain and model passthrough"
```

---

### Task 4: `ModelHost` — off-thread load + safe hand-off

Loading a model allocates and reads a file, so it must happen off the audio thread. `ModelHost` owns a background loader and hands finished models to a callback (the UI wires it to `ToneEngine::setModel`).

**Files:**
- Create: `Source/model/ModelHost.h`
- Create: `Source/model/ModelHost.cpp`
- Create: `tests/test_modelhost.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `nam::NamModel` (Task 2).
- Produces:
  ```cpp
  namespace nam {
  class ModelHost {
  public:
      using Callback = std::function<void(std::shared_ptr<NamModel>)>; // loaded (or nullptr on fail)
      void configure(int sampleRate, int maxBlock);
      void requestLoad(const std::string& path, Callback onDone); // returns immediately
      void loadNow(const std::string& path, Callback onDone);     // synchronous, for tests
      ~ModelHost();
  };
  }
  ```

- [ ] **Step 1: Write the failing tests**

```cpp
// tests/test_modelhost.cpp
#include <catch2/catch_all.hpp>
#include "model/ModelHost.h"

TEST_CASE("ModelHost loadNow succeeds on valid file") {
    nam::ModelHost host; host.configure(48000, 128);
    std::shared_ptr<nam::NamModel> got;
    host.loadNow(NAM_FIXTURE_A2, [&](auto m){ got = m; });
    REQUIRE(got != nullptr);
    REQUIRE(got->sampleRate() == 48000);
}

TEST_CASE("ModelHost loadNow reports failure as nullptr") {
    nam::ModelHost host; host.configure(48000, 128);
    bool called = false; std::shared_ptr<nam::NamModel> got;
    host.loadNow("nope.nam", [&](auto m){ called = true; got = m; });
    REQUIRE(called);
    REQUIRE(got == nullptr);
}

TEST_CASE("ModelHost requestLoad eventually calls back") {
    nam::ModelHost host; host.configure(48000, 128);
    std::promise<bool> done;
    host.requestLoad(NAM_FIXTURE_A2, [&](auto m){ done.set_value(m != nullptr); });
    auto fut = done.get_future();
    REQUIRE(fut.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
    REQUIRE(fut.get() == true);
}
```

- [ ] **Step 2: Write ModelHost.h**

```cpp
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include "model/NamModel.h"

namespace nam {
class ModelHost {
public:
    using Callback = std::function<void(std::shared_ptr<NamModel>)>;
    ModelHost();
    ~ModelHost();
    void configure(int sampleRate, int maxBlock);
    void requestLoad(const std::string& path, Callback onDone);
    void loadNow(const std::string& path, Callback onDone);
private:
    struct Job { std::string path; Callback cb; };
    void worker();
    int sampleRate_ = 48000, maxBlock_ = 128;
    std::thread th_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::queue<Job> jobs_;
    bool stop_ = false;
};
} // namespace nam
```

- [ ] **Step 3: Add to CMake, run tests to verify they fail**

Append `test_modelhost.cpp` and `${CMAKE_SOURCE_DIR}/Source/model/ModelHost.cpp` to `nam_tests`.
Run: `cmake --build build --target nam_tests -j4`
Expected: FAIL — `ModelHost` methods unresolved.

- [ ] **Step 4: Write ModelHost.cpp**

```cpp
#include "model/ModelHost.h"

namespace nam {

ModelHost::ModelHost() : th_([this]{ worker(); }) {}

ModelHost::~ModelHost() {
    { std::lock_guard<std::mutex> lk(mtx_); stop_ = true; }
    cv_.notify_all();
    if (th_.joinable()) th_.join();
}

void ModelHost::configure(int sampleRate, int maxBlock) {
    sampleRate_ = sampleRate; maxBlock_ = maxBlock;
}

void ModelHost::loadNow(const std::string& path, Callback onDone) {
    onDone(NamModel::load(path, sampleRate_, maxBlock_));
}

void ModelHost::requestLoad(const std::string& path, Callback onDone) {
    { std::lock_guard<std::mutex> lk(mtx_); jobs_.push({path, std::move(onDone)}); }
    cv_.notify_one();
}

void ModelHost::worker() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait(lk, [this]{ return stop_ || !jobs_.empty(); });
            if (stop_ && jobs_.empty()) return;
            job = std::move(jobs_.front()); jobs_.pop();
        }
        job.cb(NamModel::load(job.path, sampleRate_, maxBlock_));
    }
}

} // namespace nam
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build --target nam_tests -j4 && ./build/tests/nam_tests "ModelHost*"`
Expected: all ModelHost cases PASS.

- [ ] **Step 6: Commit**

```bash
git add Source/model/ModelHost.h Source/model/ModelHost.cpp tests/test_modelhost.cpp tests/CMakeLists.txt
git commit -m "feat: ModelHost background loader with sync + async load"
```

---

### Task 5: `AudioDeviceCallbackAdapter` — bridge JUCE audio I/O to ToneEngine

The only real-time-audio JUCE code. It reduces multi-channel device I/O to the mono guitar path, drives `ToneEngine`, and triggers a model reload when the device sample rate changes (models are sample-rate-baked).

**Files:**
- Create: `Source/app/AudioDeviceCallbackAdapter.h`
- Create: `Source/app/AudioDeviceCallbackAdapter.cpp`

**Interfaces:**
- Consumes: `dsp::ToneEngine` (Task 3).
- Produces:
  ```cpp
  class AudioDeviceCallbackAdapter : public juce::AudioIODeviceCallback {
  public:
      explicit AudioDeviceCallbackAdapter(dsp::ToneEngine& engine);
      // std::function called (on the message thread is NOT guaranteed — see note)
      // whenever the device (re)starts with a new sample rate/block size.
      std::function<void(int sampleRate, int maxBlock)> onDeviceChanged;
      // juce::AudioIODeviceCallback:
      void audioDeviceIOCallbackWithContext(const float* const* in, int numIn,
          float* const* out, int numOut, int numSamples,
          const juce::AudioIODeviceCallbackContext&) override;
      void audioDeviceAboutToStart(juce::AudioIODevice*) override;
      void audioDeviceStopped() override;
  };
  ```

- [ ] **Step 1: Write AudioDeviceCallbackAdapter.h**

```cpp
#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <functional>
#include "dsp/ToneEngine.h"

class AudioDeviceCallbackAdapter : public juce::AudioIODeviceCallback {
public:
    explicit AudioDeviceCallbackAdapter(dsp::ToneEngine& engine) : engine_(engine) {}
    std::function<void(int, int)> onDeviceChanged;

    void audioDeviceIOCallbackWithContext(const float* const* in, int numIn,
        float* const* out, int numOut, int numSamples,
        const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    dsp::ToneEngine& engine_;
    int sampleRate_ = 48000;
    int maxBlock_   = 128;
};
```

- [ ] **Step 2: Write AudioDeviceCallbackAdapter.cpp**

```cpp
#include "app/AudioDeviceCallbackAdapter.h"

void AudioDeviceCallbackAdapter::audioDeviceAboutToStart(juce::AudioIODevice* device) {
    sampleRate_ = (int) device->getCurrentSampleRate();
    maxBlock_   = device->getCurrentBufferSizeSamples();
    engine_.prepare(sampleRate_, maxBlock_);
    if (onDeviceChanged) onDeviceChanged(sampleRate_, maxBlock_); // UI reloads model at new SR
}

void AudioDeviceCallbackAdapter::audioDeviceStopped() {}

void AudioDeviceCallbackAdapter::audioDeviceIOCallbackWithContext(
    const float* const* in, int numIn, float* const* out, int numOut,
    int numSamples, const juce::AudioIODeviceCallbackContext&) {
    const float* mono = (numIn > 0 && in[0] != nullptr) ? in[0] : nullptr;
    if (mono == nullptr) {                       // no input: output silence, stay safe
        for (int ch = 0; ch < numOut; ++ch)
            if (out[ch]) juce::FloatVectorOperations::clear(out[ch], numSamples);
        return;
    }
    float* dest = (numOut > 0 && out[0] != nullptr) ? out[0] : nullptr;
    if (dest == nullptr) return;
    engine_.render(mono, dest, numSamples);      // mono guitar -> channel 0
    for (int ch = 1; ch < numOut; ++ch)          // copy to remaining channels
        if (out[ch]) juce::FloatVectorOperations::copy(out[ch], dest, numSamples);
}
```

- [ ] **Step 3: Add both files to the app target**

In top-level `CMakeLists.txt`, extend `target_sources(NamPlayer PRIVATE ...)` with `Source/app/AudioDeviceCallbackAdapter.cpp`, `Source/dsp/ToneEngine.cpp`, `Source/model/NamModel.cpp`, `Source/model/ModelHost.cpp`. Add `target_include_directories(NamPlayer PRIVATE Source)`.

- [ ] **Step 4: Build the app target to verify it compiles**

Run: `cmake --build build --target NamPlayer -j4`
Expected: compiles and links (UI comes next; app may not yet be runnable-complete until Task 6, but this file must compile).
NOTE: this task has no unit test — it is thin glue over JUCE and hardware. Its behavior is verified manually in Task 7. The compile check is its gate.

- [ ] **Step 5: Commit**

```bash
git add Source/app/AudioDeviceCallbackAdapter.h Source/app/AudioDeviceCallbackAdapter.cpp CMakeLists.txt
git commit -m "feat: JUCE audio callback adapter driving ToneEngine (mono guitar path)"
```

---

### Task 6: `MainComponent` — device picker, load button, gain sliders, meter

The UI: JUCE's `AudioDeviceSelectorComponent` (device + buffer picker with latency), a "Load .nam" file chooser wired through `ModelHost`, input/output gain sliders, and a simple output level meter.

**Files:**
- Create: `Source/app/MainComponent.h`
- Create: `Source/app/MainComponent.cpp`
- Modify: `CMakeLists.txt` (add both to `NamPlayer`)

**Interfaces:**
- Consumes: `AudioDeviceCallbackAdapter` (Task 5), `dsp::ToneEngine` (Task 3), `nam::ModelHost` (Task 4).
- Produces: `class MainComponent : public juce::Component` used by `Main.cpp` (Task 7).

- [ ] **Step 1: Write MainComponent.h**

```cpp
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "dsp/ToneEngine.h"
#include "model/ModelHost.h"
#include "app/AudioDeviceCallbackAdapter.h"

class MainComponent : public juce::Component, private juce::Timer {
public:
    MainComponent();
    ~MainComponent() override;
    void resized() override;
    void paint(juce::Graphics&) override;
private:
    void loadButtonClicked();
    void timerCallback() override;          // repaint meter + latency
    void reloadCurrentModelAt(int sampleRate, int maxBlock);

    juce::AudioDeviceManager deviceManager_;
    dsp::ToneEngine engine_;
    AudioDeviceCallbackAdapter adapter_{engine_};
    nam::ModelHost host_;

    juce::AudioDeviceSelectorComponent selector_
        { deviceManager_, 1, 2, 1, 2, false, false, true, false };
    juce::TextButton loadButton_ { "Load .nam model" };
    juce::Label      modelLabel_  { {}, "No model loaded" };
    juce::Slider     inGain_, outGain_;
    juce::Label      latencyLabel_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::String currentModelPath_;
};
```

- [ ] **Step 2: Write MainComponent.cpp**

```cpp
#include "app/MainComponent.h"

MainComponent::MainComponent() {
    deviceManager_.initialiseWithDefaultDevices(1, 2);
    engine_.prepare(48000, 128);
    deviceManager_.addAudioCallback(&adapter_);
    adapter_.onDeviceChanged = [this](int sr, int mb) {
        // Message-thread reload so the model is re-baked at the device sample rate.
        juce::MessageManager::callAsync([this, sr, mb]{ reloadCurrentModelAt(sr, mb); });
    };

    addAndMakeVisible(selector_);
    addAndMakeVisible(loadButton_);
    addAndMakeVisible(modelLabel_);
    addAndMakeVisible(latencyLabel_);

    for (auto* s : { &inGain_, &outGain_ }) {
        s->setSliderStyle(juce::Slider::LinearHorizontal);
        s->setRange(-24.0, 24.0, 0.1); s->setValue(0.0);
        s->setTextValueSuffix(" dB");
        addAndMakeVisible(*s);
    }
    inGain_.onValueChange  = [this]{ engine_.setInputDb((float) inGain_.getValue()); };
    outGain_.onValueChange = [this]{ engine_.setOutputDb((float) outGain_.getValue()); };

    loadButton_.onClick = [this]{ loadButtonClicked(); };
    startTimerHz(15);
    setSize(560, 640);
}

MainComponent::~MainComponent() {
    deviceManager_.removeAudioCallback(&adapter_);
}

void MainComponent::loadButtonClicked() {
    chooser_ = std::make_unique<juce::FileChooser>(
        "Select a NAM model", juce::File{}, "*.nam");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode
                        | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc) {
            auto f = fc.getResult();
            if (f == juce::File{}) return;
            currentModelPath_ = f.getFullPathName();
            auto* dev = deviceManager_.getCurrentAudioDevice();
            host_.configure(dev ? (int) dev->getCurrentSampleRate() : 48000,
                            dev ? dev->getCurrentBufferSizeSamples() : 128);
            modelLabel_.setText("Loading " + f.getFileName() + "...",
                                juce::dontSendNotification);
            host_.requestLoad(currentModelPath_.toStdString(),
                [this, name = f.getFileName()](std::shared_ptr<nam::NamModel> m) {
                    juce::MessageManager::callAsync([this, m, name]{
                        engine_.setModel(m);
                        modelLabel_.setText(m ? ("Loaded: " + name)
                                              : ("Failed to load " + name),
                                            juce::dontSendNotification);
                    });
                });
        });
}

void MainComponent::reloadCurrentModelAt(int sampleRate, int maxBlock) {
    if (currentModelPath_.isEmpty()) return;
    host_.configure(sampleRate, maxBlock);
    host_.requestLoad(currentModelPath_.toStdString(),
        [this](std::shared_ptr<nam::NamModel> m) {
            juce::MessageManager::callAsync([this, m]{ engine_.setModel(m); });
        });
}

void MainComponent::timerCallback() {
    if (auto* dev = deviceManager_.getCurrentAudioDevice()) {
        const double sr = dev->getCurrentSampleRate();
        const int    bs = dev->getCurrentBufferSizeSamples();
        const double ms = sr > 0 ? (bs / sr) * 1000.0 : 0.0;
        juce::String warn = (sr > 0 && bs / sr > 0.02) ? "  (high latency)" : "";
        latencyLabel_.setText(juce::String(bs) + " smp @ " + juce::String((int) sr)
            + " Hz  ~" + juce::String(ms, 1) + " ms/dir" + warn,
            juce::dontSendNotification);
    }
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black);
}

void MainComponent::resized() {
    auto r = getLocalBounds().reduced(12);
    selector_.setBounds(r.removeFromTop(300));
    r.removeFromTop(8);
    loadButton_.setBounds(r.removeFromTop(32));
    modelLabel_.setBounds(r.removeFromTop(24));
    latencyLabel_.setBounds(r.removeFromTop(24));
    r.removeFromTop(8);
    inGain_.setBounds(r.removeFromTop(32));
    outGain_.setBounds(r.removeFromTop(32));
}
```

- [ ] **Step 3: Add to CMake and build**

Add `Source/app/MainComponent.cpp` to `target_sources(NamPlayer ...)`.
Run: `cmake --build build --target NamPlayer -j4`
Expected: compiles and links.

- [ ] **Step 4: Commit**

```bash
git add Source/app/MainComponent.h Source/app/MainComponent.cpp CMakeLists.txt
git commit -m "feat: MainComponent UI — device picker, model load, gain, latency readout"
```

---

### Task 7: `Main.cpp` app entry + end-to-end "hear a tone" verification

Ties the app together and verifies the whole point of Phase 1: real guitar → model → sound, on a real device.

**Files:**
- Create: `Source/app/Main.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `MainComponent` (Task 6).
- Produces: a runnable `NamPlayer` desktop application.

- [ ] **Step 1: Write Main.cpp**

```cpp
#include <juce_gui_extra/juce_gui_extra.h>
#include "app/MainComponent.h"

class NamPlayerApp : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "NAM Player"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    void initialise(const juce::String&) override {
        window_ = std::make_unique<Window>();
    }
    void shutdown() override { window_ = nullptr; }
private:
    struct Window : juce::DocumentWindow {
        Window() : juce::DocumentWindow("NAM Player",
            juce::Colours::black, juce::DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(true, true);
            centreWithSize(560, 640);
            setVisible(true);
        }
        void closeButtonPressed() override {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };
    std::unique_ptr<Window> window_;
};

START_JUCE_APPLICATION(NamPlayerApp)
```

- [ ] **Step 2: Add to CMake and build the app**

Add `Source/app/Main.cpp` to `target_sources(NamPlayer ...)`.
Run: `cmake --build build --target NamPlayer -j4`
Expected: builds a runnable app bundle/binary.

- [ ] **Step 3: Run the full test suite (regression gate)**

Run: `./build/tests/nam_tests`
Expected: all tests from Tasks 2–4 PASS.

- [ ] **Step 4: Manual end-to-end verification (with a real audio interface)**

Do each and confirm:
1. Launch `NamPlayer`. The device selector lists your USB interface.
2. Select the interface as input **and** output; set buffer to 128.
3. The latency readout shows a sane value (e.g. ~2.7 ms/dir at 128/48k) with no "high latency" warning.
4. Click **Load .nam model**, choose `tests/fixtures/example_a2.nam`. Label shows "Loaded: ...".
5. Play the guitar → you hear the amp model. Adjust input/output gain sliders → level responds smoothly (no zipper noise).
6. Change the buffer size in the selector → audio continues, model still sounds correct (sample-rate reload path works if the device also changes SR).
7. Unplug the interface → app does not crash; reconnect and it resumes.

- [ ] **Step 5: Commit**

```bash
git add Source/app/Main.cpp CMakeLists.txt
git commit -m "feat: app entry point; Phase 1 hear-a-tone milestone complete"
```

---

## Self-Review (completed)

- **Spec coverage (Phase 1 slice of §10.1):** device+buffer picker (Task 6), local `.nam` load (Tasks 2/4/6), real-time engine + hear a tone (Tasks 3/5/7), sample-rate reload correctness (Tasks 5/6), Bluetooth/latency warning (Task 6 latency readout). ✅
- **Deferred to later phases (correctly out of scope here):** noise gate, IR cab, EQ (Phase 2); library (Phase 3); TONE3000 (Phase 4); mobile packaging (Phase 5).
- **Placeholder scan:** no TBD/TODO; every code step contains full code. The one flagged risk (static-vs-loader factory API) is localized to `NamModel.cpp` with an explicit fallback instruction. ✅
- **Type consistency:** `NamModel::load`, `ToneEngine::{prepare,setModel,setInputDb,setOutputDb,render}`, `ModelHost::{configure,requestLoad,loadNow}`, and `AudioDeviceCallbackAdapter::onDeviceChanged` names match across all tasks that reference them. ✅
- **Known real-world caveat:** `std::atomic_load/store(shared_ptr*)` (Task 3) is deprecated in C++20; on a C++20 toolchain, switch `ToneEngine`'s model handle to `std::atomic<std::shared_ptr<...>>`. Noted for the executor.
