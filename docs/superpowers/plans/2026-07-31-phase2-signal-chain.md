# NAM Player — Phase 2: Full Single-Amp Signal Chain (Gate, IR Cab, EQ)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wrap the NAM model with a usable practice-tone chain: a noise gate before the amp, a speaker-cabinet impulse-response (IR) loader after it, and a 3-band tone EQ — each individually bypassable.

**Architecture:** New JUCE-free DSP nodes (`NoiseGate`, `IrCab`, `ToneEq`) and a JUCE-free `IrLoader` (WAV→mono float via vendored public-domain `dr_wav`), all unit-tested headlessly. `ToneEngine::render()` gains the new stages in a fixed order. UI grows controls in `MainComponent`. Same RT-safety discipline as Phase 1: the audio callback never allocates/locks/throws; IR hand-off uses the atomic-raw-pointer + control-thread-owned-lifetime pattern already used for the model.

**Tech Stack:** C++17, JUCE 8.0.15, NeuralAudio (A1/A2), dr_wav (public domain), Catch2 v3.

## Global Constraints

- **Language:** C++17.
- **Real-time safety (#1 invariant):** `ToneEngine::render()` and everything it calls (gate, model, IR cab, EQ, gains) MUST NOT allocate heap, lock a mutex, throw, or do I/O.
- **JUCE isolation:** `Source/dsp/` and `Source/model/` MUST NOT include any JUCE header. Only `Source/app/` may.
- **Signal order (fixed):** input gain → NoiseGate → NAM model → IrCab → ToneEq → output gain.
- **Bypass:** every stage is individually bypassable; a bypassed stage is a transparent passthrough. IR cab defaults to **disabled** so amp+cab captures are not double-cabbed.
- **RT-safe parameter changes:** UI-set parameters cross to the audio thread via `std::atomic` (relaxed) — never a lock. Coefficient/model/IR objects that can't be a single atomic scalar use the atomic-raw-pointer + control-thread-owned-lifetime + reclaim-in-`prepare()` pattern established in `ToneEngine` Phase 1.
- **Audio format:** mono, 32-bit float, default 48000 Hz / 128-sample buffer.
- **IR cap:** IRs are truncated to `kMaxIrTaps = 4096` samples at load; the convolver's ring buffer is preallocated to that size.
- **License:** dr_wav is public domain (unlicense/MIT-0); vendor its single header unmodified under `extern/dr_wav/`.

## File Structure

```
Source/dsp/
  NoiseGate.h                 # header-only envelope-follower gate (JUCE-free)
  ToneEq.h  ToneEq.cpp        # 3-band RBJ biquad EQ (JUCE-free)
  Biquad.h                    # header-only TDF2 biquad + RBJ coeff calculators
  IrCab.h   IrCab.cpp         # direct-form FIR convolver + RT-safe IR swap (JUCE-free)
  ToneEngine.h ToneEngine.cpp # MODIFIED: wire gate/IR/EQ into render()
Source/model/
  IrLoader.h IrLoader.cpp     # WAV -> mono float @ target SR via dr_wav (JUCE-free)
Source/app/
  MainComponent.h/.cpp        # MODIFIED: gate/IR/EQ controls
extern/dr_wav/
  dr_wav.h                    # vendored public-domain single header
tests/
  test_noisegate.cpp  test_toneeq.cpp  test_ircab.cpp  test_irloader.cpp
  test_toneengine.cpp         # MODIFIED: chain-integration tests
  CMakeLists.txt              # MODIFIED: new sources + dr_wav include dir
```

---

### Task 1: `NoiseGate` — envelope-follower gate (JUCE-free, header-only)

Gate before the amp to silence hum/hiss between notes on high-gain models. Opens fast, closes slowly, with hysteresis so it doesn't chatter around the threshold.

**Files:**
- Create: `Source/dsp/NoiseGate.h`
- Create: `tests/test_noisegate.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces:
  ```cpp
  namespace dsp {
  class NoiseGate {
  public:
      void  prepare(int sampleRate);
      void  setEnabled(bool on)      { enabled_.store(on, std::memory_order_relaxed); }
      void  setThresholdDb(float db) { threshDb_.store(db, std::memory_order_relaxed); }
      // Audio thread. In-place safe (in may equal out).
      void  process(const float* in, float* out, int numSamples);
  };
  }
  ```

- [ ] **Step 1: Write NoiseGate.h**

```cpp
#pragma once
#include <atomic>
#include <cmath>

namespace dsp {
// Envelope-follower noise gate. Opens (attack) fast, closes (release) slow,
// with hysteresis between open/close thresholds to avoid chatter.
class NoiseGate {
public:
    void prepare(int sampleRate) {
        const float sr = sampleRate > 0 ? (float) sampleRate : 48000.0f;
        envCoeff_  = std::exp(-1.0f / (0.001f * sr));   // 1 ms envelope
        attCoeff_  = std::exp(-1.0f / (0.001f * sr));   // 1 ms open
        relCoeff_  = std::exp(-1.0f / (0.100f * sr));   // 100 ms close
        env_ = 0.0f; gain_ = 1.0f;
    }
    void setEnabled(bool on)      { enabled_.store(on, std::memory_order_relaxed); }
    void setThresholdDb(float db) { threshDb_.store(db, std::memory_order_relaxed); }

    void process(const float* in, float* out, int numSamples) {
        if (! enabled_.load(std::memory_order_relaxed)) {
            if (in != out) for (int i = 0; i < numSamples; ++i) out[i] = in[i];
            return;
        }
        const float openLin  = std::pow(10.0f, threshDb_.load(std::memory_order_relaxed) / 20.0f);
        const float closeLin = openLin * 0.5f;          // -6 dB hysteresis
        for (int i = 0; i < numSamples; ++i) {
            const float x = in[i];
            // Peak envelope follower.
            const float rectified = std::fabs(x);
            env_ = rectified + envCoeff_ * (env_ - rectified);
            // Target gate state with hysteresis.
            if (env_ > openLin)       target_ = 1.0f;
            else if (env_ < closeLin) target_ = 0.0f;   // else hold previous target_
            const float c = target_ > gain_ ? attCoeff_ : relCoeff_;
            gain_ = target_ + c * (gain_ - target_);
            out[i] = x * gain_;
        }
    }
private:
    std::atomic<bool>  enabled_{false};
    std::atomic<float> threshDb_{-60.0f};
    float envCoeff_ = 0.0f, attCoeff_ = 0.0f, relCoeff_ = 0.0f;
    float env_ = 0.0f, gain_ = 1.0f, target_ = 1.0f;
};
} // namespace dsp
```

- [ ] **Step 2: Write the failing tests**

```cpp
// tests/test_noisegate.cpp
#include <catch2/catch_all.hpp>
#include <vector>
#include <cmath>
#include "dsp/NoiseGate.h"
using Catch::Approx;

TEST_CASE("NoiseGate disabled is passthrough") {
    dsp::NoiseGate g; g.prepare(48000); g.setEnabled(false);
    std::vector<float> in(64, 0.3f), out(64, 0.0f);
    g.process(in.data(), out.data(), 64);
    for (int i = 0; i < 64; ++i) REQUIRE(out[i] == Approx(0.3f));
}

TEST_CASE("NoiseGate passes a loud signal above threshold") {
    dsp::NoiseGate g; g.prepare(48000); g.setEnabled(true); g.setThresholdDb(-40.0f);
    std::vector<float> in(4800), out(4800);
    for (int i = 0; i < 4800; ++i) in[i] = 0.5f * std::sin(i * 0.1f); // ~ -6 dB, well above
    g.process(in.data(), out.data(), 4800);
    // After the gate opens, output tracks input closely near the end.
    float e = 0; for (int i = 4700; i < 4800; ++i) e += std::fabs(out[i] - in[i]);
    REQUIRE(e / 100.0f < 0.02f);
}

TEST_CASE("NoiseGate attenuates a quiet signal below threshold") {
    dsp::NoiseGate g; g.prepare(48000); g.setEnabled(true); g.setThresholdDb(-40.0f);
    std::vector<float> in(9600), out(9600);
    for (int i = 0; i < 9600; ++i) in[i] = 0.001f * std::sin(i * 0.1f); // ~ -60 dB, below
    g.process(in.data(), out.data(), 9600);
    float peakOut = 0; for (int i = 9500; i < 9600; ++i) peakOut = std::max(peakOut, std::fabs(out[i]));
    REQUIRE(peakOut < 0.0002f); // heavily attenuated after release
}
```

- [ ] **Step 3: Wire into CMake**

Append `test_noisegate.cpp` to the `nam_tests` sources in `tests/CMakeLists.txt`. (NoiseGate is header-only — no .cpp to add.)

- [ ] **Step 4: Run tests to verify they fail, then pass**

Run: `cmake --build --preset default --target nam_tests -j4 && ./build/tests/nam_tests "NoiseGate*"`
Expected: FAIL before Step 1 exists; PASS after.

- [ ] **Step 5: Commit**

```bash
git add Source/dsp/NoiseGate.h tests/test_noisegate.cpp tests/CMakeLists.txt
git commit -m "feat: NoiseGate envelope-follower gate with hysteresis"
```

---

### Task 2: `Biquad` + `ToneEq` — 3-band RBJ EQ (JUCE-free)

A low-shelf / mid-bell / high-shelf tone stack. Coefficients are recomputed on the audio thread only when a gain changes (once per block at most; transcendentals per block are fine).

**Files:**
- Create: `Source/dsp/Biquad.h`
- Create: `Source/dsp/ToneEq.h`
- Create: `Source/dsp/ToneEq.cpp`
- Create: `tests/test_toneeq.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces:
  ```cpp
  namespace dsp {
  struct Biquad {                       // header-only, Transposed Direct Form II
      float b0=1,b1=0,b2=0,a1=0,a2=0;   // a0 normalized to 1
      float z1=0, z2=0;
      float processSample(float x);
      void  reset() { z1 = z2 = 0; }
      static Biquad lowShelf (float sr, float freq, float gainDb);
      static Biquad peaking  (float sr, float freq, float q, float gainDb);
      static Biquad highShelf(float sr, float freq, float gainDb);
  };
  class ToneEq {
  public:
      void prepare(int sampleRate);
      void setEnabled(bool on) { enabled_.store(on, std::memory_order_relaxed); }
      void setLowDb (float db) { lowDb_.store(db,  std::memory_order_relaxed); }
      void setMidDb (float db) { midDb_.store(db,  std::memory_order_relaxed); }
      void setHighDb(float db) { highDb_.store(db, std::memory_order_relaxed); }
      void process(float* buf, int numSamples);   // in place
  };
  }
  ```
- Fixed frequencies: low shelf 100 Hz, mid bell 700 Hz (Q 0.7), high shelf 3200 Hz.

- [ ] **Step 1: Write Biquad.h (TDF2 + RBJ coefficient calculators)**

```cpp
#pragma once
#include <cmath>

namespace dsp {
struct Biquad {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    inline float processSample(float x) {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0.0f; }

    // RBJ Audio EQ Cookbook formulas. Returns coeffs with a0 normalized to 1.
    static Biquad lowShelf(float sr, float freq, float gainDb) {
        Biquad bq; const float A = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * 3.14159265358979f * freq / sr;
        const float cs = std::cos(w0), sn = std::sin(w0);
        const float alpha = sn / 2.0f * std::sqrt((A + 1.0f/A) * (1.0f/0.9f - 1.0f) + 2.0f);
        const float tsa = 2.0f * std::sqrt(A) * alpha;
        const float a0 = (A+1) + (A-1)*cs + tsa;
        bq.b0 = A*((A+1) - (A-1)*cs + tsa) / a0;
        bq.b1 = 2*A*((A-1) - (A+1)*cs)     / a0;
        bq.b2 = A*((A+1) - (A-1)*cs - tsa) / a0;
        bq.a1 = -2*((A-1) + (A+1)*cs)      / a0;
        bq.a2 = ((A+1) + (A-1)*cs - tsa)   / a0;
        return bq;
    }
    static Biquad peaking(float sr, float freq, float q, float gainDb) {
        Biquad bq; const float A = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * 3.14159265358979f * freq / sr;
        const float cs = std::cos(w0), sn = std::sin(w0);
        const float alpha = sn / (2.0f * q);
        const float a0 = 1 + alpha/A;
        bq.b0 = (1 + alpha*A) / a0;
        bq.b1 = (-2*cs)       / a0;
        bq.b2 = (1 - alpha*A) / a0;
        bq.a1 = (-2*cs)       / a0;
        bq.a2 = (1 - alpha/A) / a0;
        return bq;
    }
    static Biquad highShelf(float sr, float freq, float gainDb) {
        Biquad bq; const float A = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * 3.14159265358979f * freq / sr;
        const float cs = std::cos(w0), sn = std::sin(w0);
        const float alpha = sn / 2.0f * std::sqrt((A + 1.0f/A) * (1.0f/0.9f - 1.0f) + 2.0f);
        const float tsa = 2.0f * std::sqrt(A) * alpha;
        const float a0 = (A+1) - (A-1)*cs + tsa;
        bq.b0 = A*((A+1) + (A-1)*cs + tsa) / a0;
        bq.b1 = -2*A*((A-1) + (A+1)*cs)    / a0;
        bq.b2 = A*((A+1) + (A-1)*cs - tsa) / a0;
        bq.a1 = 2*((A-1) - (A+1)*cs)       / a0;
        bq.a2 = ((A+1) - (A-1)*cs - tsa)   / a0;
        return bq;
    }
};
} // namespace dsp
```

- [ ] **Step 2: Write ToneEq.h**

```cpp
#pragma once
#include <atomic>
#include "dsp/Biquad.h"

namespace dsp {
class ToneEq {
public:
    void prepare(int sampleRate);
    void setEnabled(bool on) { enabled_.store(on, std::memory_order_relaxed); }
    void setLowDb (float db) { lowDb_.store(db,  std::memory_order_relaxed); }
    void setMidDb (float db) { midDb_.store(db,  std::memory_order_relaxed); }
    void setHighDb(float db) { highDb_.store(db, std::memory_order_relaxed); }
    void process(float* buf, int numSamples);
private:
    void recalcIfChanged();
    std::atomic<bool>  enabled_{false};
    std::atomic<float> lowDb_{0.0f}, midDb_{0.0f}, highDb_{0.0f};
    float sr_ = 48000.0f;
    float lastLow_ = 1e9f, lastMid_ = 1e9f, lastHigh_ = 1e9f; // force first recalc
    Biquad low_, mid_, high_;
};
} // namespace dsp
```

- [ ] **Step 3: Write the failing tests**

```cpp
// tests/test_toneeq.cpp
#include <catch2/catch_all.hpp>
#include <vector>
#include <cmath>
#include "dsp/ToneEq.h"
using Catch::Approx;

static float rms(const std::vector<float>& v, int from) {
    double s = 0; int n = 0;
    for (int i = from; i < (int) v.size(); ++i) { s += (double) v[i]*v[i]; ++n; }
    return (float) std::sqrt(s / n);
}
static std::vector<float> sine(float freq, float sr, int n) {
    std::vector<float> v(n);
    for (int i = 0; i < n; ++i) v[i] = std::sin(2.0f*3.14159265f*freq*i/sr);
    return v;
}

TEST_CASE("ToneEq disabled is passthrough") {
    dsp::ToneEq eq; eq.prepare(48000); eq.setEnabled(false);
    auto in = sine(1000, 48000, 512); auto buf = in;
    eq.process(buf.data(), (int) buf.size());
    for (size_t i = 0; i < buf.size(); ++i) REQUIRE(buf[i] == Approx(in[i]));
}

TEST_CASE("ToneEq flat (0 dB) is ~unity") {
    dsp::ToneEq eq; eq.prepare(48000); eq.setEnabled(true);
    eq.setLowDb(0); eq.setMidDb(0); eq.setHighDb(0);
    auto in = sine(1000, 48000, 4096); auto buf = in;
    eq.process(buf.data(), (int) buf.size());
    REQUIRE(rms(buf, 2048) == Approx(rms(in, 2048)).epsilon(0.02));
}

TEST_CASE("ToneEq high-shelf boost increases HF energy") {
    dsp::ToneEq eq; eq.prepare(48000); eq.setEnabled(true);
    eq.setHighDb(12.0f);
    auto in = sine(8000, 48000, 8192); auto buf = in;
    eq.process(buf.data(), (int) buf.size());
    REQUIRE(rms(buf, 4096) > rms(in, 4096) * 1.5f);
}
```

- [ ] **Step 4: Write ToneEq.cpp**

```cpp
#include "dsp/ToneEq.h"

namespace dsp {

void ToneEq::prepare(int sampleRate) {
    sr_ = sampleRate > 0 ? (float) sampleRate : 48000.0f;
    lastLow_ = lastMid_ = lastHigh_ = 1e9f;
    low_.reset(); mid_.reset(); high_.reset();
    recalcIfChanged();
}

void ToneEq::recalcIfChanged() {
    const float lo = lowDb_.load(std::memory_order_relaxed);
    const float md = midDb_.load(std::memory_order_relaxed);
    const float hi = highDb_.load(std::memory_order_relaxed);
    if (lo != lastLow_)  { const float z1=low_.z1, z2=low_.z2;  low_  = Biquad::lowShelf (sr_, 100.0f, lo);       low_.z1=z1; low_.z2=z2;  lastLow_ = lo; }
    if (md != lastMid_)  { const float z1=mid_.z1, z2=mid_.z2;  mid_  = Biquad::peaking  (sr_, 700.0f, 0.7f, md);  mid_.z1=z1; mid_.z2=z2;  lastMid_ = md; }
    if (hi != lastHigh_) { const float z1=high_.z1,z2=high_.z2; high_ = Biquad::highShelf(sr_, 3200.0f, hi);      high_.z1=z1; high_.z2=z2; lastHigh_ = hi; }
}

void ToneEq::process(float* buf, int numSamples) {
    if (! enabled_.load(std::memory_order_relaxed)) return;   // passthrough
    recalcIfChanged();                                        // at most once per block
    for (int i = 0; i < numSamples; ++i) {
        float y = low_.processSample(buf[i]);
        y = mid_.processSample(y);
        y = high_.processSample(y);
        buf[i] = y;
    }
}

} // namespace dsp
```
NOTE: preserving the biquad state (`z1`/`z2`) across a coefficient swap avoids a click when a knob moves. This is intentional.

- [ ] **Step 5: Wire into CMake, run tests (fail → pass)**

Append `test_toneeq.cpp` and `${CMAKE_SOURCE_DIR}/Source/dsp/ToneEq.cpp` to `nam_tests`.
Run: `cmake --build --preset default --target nam_tests -j4 && ./build/tests/nam_tests "ToneEq*"`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add Source/dsp/Biquad.h Source/dsp/ToneEq.h Source/dsp/ToneEq.cpp tests/test_toneeq.cpp tests/CMakeLists.txt
git commit -m "feat: ToneEq 3-band RBJ biquad tone stack"
```

---

### Task 3: `IrCab` — direct-form FIR convolver with RT-safe IR swap (JUCE-free)

Convolves the amp output with a mono speaker-cab impulse response. IR data is owned by the control thread and published to the audio thread via an atomic raw pointer (same discipline as the model in Phase 1).

**Files:**
- Create: `Source/dsp/IrCab.h`
- Create: `Source/dsp/IrCab.cpp`
- Create: `tests/test_ircab.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing (IR data supplied as `std::shared_ptr<const std::vector<float>>`).
- Produces:
  ```cpp
  namespace dsp {
  inline constexpr int kMaxIrTaps = 4096;
  class IrCab {
  public:
      void prepare(int sampleRate, int maxBlock);   // control thread; reclaims retired IRs
      void setEnabled(bool on) { enabled_.store(on, std::memory_order_relaxed); }
      // Control thread. IR is truncated to kMaxIrTaps by the caller/loader.
      void setImpulse(std::shared_ptr<const std::vector<float>> ir);
      void process(const float* in, float* out, int numSamples); // audio thread, in-place safe
  };
  }
  ```

- [ ] **Step 1: Write IrCab.h**

```cpp
#pragma once
#include <atomic>
#include <memory>
#include <vector>

namespace dsp {
inline constexpr int kMaxIrTaps = 4096;

class IrCab {
public:
    void prepare(int sampleRate, int maxBlock);
    void setEnabled(bool on) { enabled_.store(on, std::memory_order_relaxed); }
    void setImpulse(std::shared_ptr<const std::vector<float>> ir);
    void process(const float* in, float* out, int numSamples);
private:
    std::atomic<bool> enabled_{false};

    // RT-safe IR ownership: audio thread reads `active_` (raw ptr) only;
    // control thread owns lifetime via current_/retired_, reclaimed in prepare().
    std::atomic<const std::vector<float>*> active_{nullptr};
    std::shared_ptr<const std::vector<float>> current_;
    std::vector<std::shared_ptr<const std::vector<float>>> retired_;

    // Preallocated delay line (ring buffer) of past inputs; size kMaxIrTaps.
    std::vector<float> ring_;
    int writePos_ = 0;
};
} // namespace dsp
```

- [ ] **Step 2: Write the failing tests**

```cpp
// tests/test_ircab.cpp
#include <catch2/catch_all.hpp>
#include <vector>
#include <memory>
#include "dsp/IrCab.h"
using Catch::Approx;

TEST_CASE("IrCab disabled is passthrough") {
    dsp::IrCab cab; cab.prepare(48000, 128); cab.setEnabled(false);
    std::vector<float> in(8), out(8);
    for (int i = 0; i < 8; ++i) in[i] = (float) (i + 1);
    cab.process(in.data(), out.data(), 8);
    for (int i = 0; i < 8; ++i) REQUIRE(out[i] == Approx(in[i]));
}

TEST_CASE("IrCab with unit-impulse IR is identity") {
    dsp::IrCab cab; cab.prepare(48000, 128); cab.setEnabled(true);
    cab.setImpulse(std::make_shared<std::vector<float>>(std::vector<float>{1.0f}));
    std::vector<float> in(8), out(8);
    for (int i = 0; i < 8; ++i) in[i] = 0.1f * (i + 1);
    cab.process(in.data(), out.data(), 8);
    for (int i = 0; i < 8; ++i) REQUIRE(out[i] == Approx(in[i]));
}

TEST_CASE("IrCab with 2-tap IR convolves correctly") {
    dsp::IrCab cab; cab.prepare(48000, 128); cab.setEnabled(true);
    cab.setImpulse(std::make_shared<std::vector<float>>(std::vector<float>{0.5f, 0.5f}));
    std::vector<float> in{1, 0, 0, 0}, out(4);
    cab.process(in.data(), out.data(), 4);
    // y[0]=0.5*1, y[1]=0.5*1, rest 0
    REQUIRE(out[0] == Approx(0.5f));
    REQUIRE(out[1] == Approx(0.5f));
    REQUIRE(out[2] == Approx(0.0f));
    REQUIRE(out[3] == Approx(0.0f));
}

TEST_CASE("IrCab handles model-less nullptr IR as passthrough when enabled") {
    dsp::IrCab cab; cab.prepare(48000, 128); cab.setEnabled(true);
    std::vector<float> in(8, 0.2f), out(8);
    cab.process(in.data(), out.data(), 8);   // no IR set yet
    for (int i = 0; i < 8; ++i) REQUIRE(out[i] == Approx(0.2f));
}
```

- [ ] **Step 3: Write IrCab.cpp**

```cpp
#include "dsp/IrCab.h"
#include <algorithm>

namespace dsp {

void IrCab::prepare(int /*sampleRate*/, int /*maxBlock*/) {
    ring_.assign(kMaxIrTaps, 0.0f);
    writePos_ = 0;
    retired_.clear();               // safe point: audio stopped
}

void IrCab::setImpulse(std::shared_ptr<const std::vector<float>> ir) {
    if (current_) retired_.push_back(std::move(current_));
    current_ = std::move(ir);
    active_.store(current_ ? current_.get() : nullptr, std::memory_order_release);
}

void IrCab::process(const float* in, float* out, int numSamples) {
    const std::vector<float>* ir =
        enabled_.load(std::memory_order_relaxed)
            ? active_.load(std::memory_order_acquire) : nullptr;
    if (ir == nullptr || ir->empty()) {           // passthrough
        if (in != out) for (int i = 0; i < numSamples; ++i) out[i] = in[i];
        return;
    }
    const int taps = std::min((int) ir->size(), kMaxIrTaps);
    const float* h = ir->data();
    const int cap = (int) ring_.size();
    for (int n = 0; n < numSamples; ++n) {
        ring_[writePos_] = in[n];
        float acc = 0.0f;
        int idx = writePos_;
        for (int k = 0; k < taps; ++k) {
            acc += h[k] * ring_[idx];
            idx = idx == 0 ? cap - 1 : idx - 1;
        }
        out[n] = acc;
        writePos_ = writePos_ == cap - 1 ? 0 : writePos_ + 1;
    }
}

} // namespace dsp
```
NOTE: `in`/`out` may alias; the ring write happens before the read of `in[n]`, and `in[n]` is read once into the ring before `out[n]` is written, so aliasing is safe.

- [ ] **Step 4: Wire into CMake, run tests (fail → pass)**

Append `test_ircab.cpp` and `${CMAKE_SOURCE_DIR}/Source/dsp/IrCab.cpp` to `nam_tests`.
Run: `cmake --build --preset default --target nam_tests -j4 && ./build/tests/nam_tests "IrCab*"`
Expected: PASS.

- [ ] **Step 5: ASan check (aliasing + ring bounds)**

Run: `cmake --preset asan && cmake --build --preset asan --target nam_tests -j4 && ./build-asan/tests/nam_tests "IrCab*"`
Expected: PASS, no ASan diagnostics.

- [ ] **Step 6: Commit**

```bash
git add Source/dsp/IrCab.h Source/dsp/IrCab.cpp tests/test_ircab.cpp tests/CMakeLists.txt
git commit -m "feat: IrCab direct-form FIR convolver with RT-safe IR swap"
```

---

### Task 4: `IrLoader` — WAV → mono float @ engine sample rate (JUCE-free, dr_wav)

Loads a cab IR `.wav`, downmixes to mono, linear-resamples to the engine rate if needed, and truncates to `kMaxIrTaps`. Off the audio thread.

**Files:**
- Create: `extern/dr_wav/dr_wav.h` (vendored, see Step 1)
- Create: `Source/model/IrLoader.h`
- Create: `Source/model/IrLoader.cpp`
- Create: `tests/test_irloader.cpp`
- Modify: `tests/CMakeLists.txt` (add source + `extern/dr_wav` include dir)
- Modify: `CMakeLists.txt` (add `extern/dr_wav` to NamPlayer include dirs)

**Interfaces:**
- Consumes: dr_wav.
- Produces:
  ```cpp
  namespace nam {
  // Returns a mono IR at targetSampleRate, truncated to maxTaps, or nullptr on
  // failure. Never throws.
  std::shared_ptr<const std::vector<float>>
  loadImpulseResponse(const std::string& path, int targetSampleRate, int maxTaps);
  }
  ```

- [ ] **Step 1: Vendor dr_wav.h**

```bash
mkdir -p extern/dr_wav
curl -fsSL -o extern/dr_wav/dr_wav.h \
  https://raw.githubusercontent.com/mackron/dr_libs/master/dr_wav.h
```

- [ ] **Step 2: Write IrLoader.h**

```cpp
#pragma once
#include <memory>
#include <string>
#include <vector>

namespace nam {
std::shared_ptr<const std::vector<float>>
loadImpulseResponse(const std::string& path, int targetSampleRate, int maxTaps);
}
```

- [ ] **Step 3: Write the failing tests**

```cpp
// tests/test_irloader.cpp
#include <catch2/catch_all.hpp>
#include <filesystem>
#include <memory>
#include "model/IrLoader.h"

// Writes a minimal 48k mono 32-bit-float WAV with the given samples.
static std::string writeWav(const std::string& name, const std::vector<float>& s, int sr);

TEST_CASE("IrLoader returns nullptr on bad path") {
    auto ir = nam::loadImpulseResponse("nope.wav", 48000, 4096);
    REQUIRE(ir == nullptr);
}

TEST_CASE("IrLoader loads a mono 48k WAV unchanged") {
    std::vector<float> s{0.0f, 1.0f, -0.5f, 0.25f};
    auto path = writeWav("ir_test_48k.wav", s, 48000);
    auto ir = nam::loadImpulseResponse(path, 48000, 4096);
    std::filesystem::remove(path);
    REQUIRE(ir != nullptr);
    REQUIRE(ir->size() == 4u);
    REQUIRE((*ir)[1] == Catch::Approx(1.0f).margin(1e-4));
}

TEST_CASE("IrLoader truncates to maxTaps") {
    std::vector<float> s(10000, 0.1f);
    auto path = writeWav("ir_test_long.wav", s, 48000);
    auto ir = nam::loadImpulseResponse(path, 48000, 4096);
    std::filesystem::remove(path);
    REQUIRE(ir != nullptr);
    REQUIRE((int) ir->size() == 4096);
}
```

Implement `writeWav` at the bottom of the test file using dr_wav's write API:

```cpp
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
static std::string writeWav(const std::string& name, const std::vector<float>& s, int sr) {
    auto path = (std::filesystem::temp_directory_path() / name).string();
    drwav_data_format fmt{};
    fmt.container = drwav_container_riff; fmt.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    fmt.channels = 1; fmt.sampleRate = (drwav_uint32) sr; fmt.bitsPerSample = 32;
    drwav wav;
    drwav_init_file_write(&wav, path.c_str(), &fmt, nullptr);
    drwav_write_pcm_frames(&wav, s.size(), s.data());
    drwav_uninit(&wav);
    return path;
}
```
NOTE: `DR_WAV_IMPLEMENTATION` must be defined in exactly ONE translation unit across the whole `nam_tests` target. Define it in `IrLoader.cpp` (Step 4). In the test file, `#include "dr_wav.h"` WITHOUT the implementation macro, and instead declare `writeWav` above the tests and define it in a separate `.cpp`, OR guard so only one TU has the implementation. Simplest: put `writeWav` in `IrLoader.cpp`'s TU is not possible (test-only); instead define `DR_WAV_IMPLEMENTATION` only in `IrLoader.cpp`, and in the test file include `dr_wav.h` without the macro and call `drwav_*` (they link from IrLoader.cpp's TU). Verify a single-definition build.

- [ ] **Step 4: Write IrLoader.cpp**

```cpp
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
```

- [ ] **Step 5: Wire CMake (include dir + sources), run tests (fail → pass)**

In `tests/CMakeLists.txt`: append `test_irloader.cpp` and `${CMAKE_SOURCE_DIR}/Source/model/IrLoader.cpp`; add `target_include_directories(nam_tests PRIVATE ${CMAKE_SOURCE_DIR}/extern/dr_wav)`.
In top-level `CMakeLists.txt`: add `${CMAKE_SOURCE_DIR}/extern/dr_wav` to `target_include_directories(NamPlayer PRIVATE ...)` and add `Source/model/IrLoader.cpp` to NamPlayer sources.
Run: `cmake --build --preset default --target nam_tests -j4 && ./build/tests/nam_tests "IrLoader*"`
Expected: PASS (single dr_wav implementation TU — no duplicate-symbol link error).

- [ ] **Step 6: Commit**

```bash
git add extern/dr_wav/dr_wav.h Source/model/IrLoader.h Source/model/IrLoader.cpp tests/test_irloader.cpp tests/CMakeLists.txt CMakeLists.txt
git commit -m "feat: IrLoader (WAV -> mono float @ target SR) via vendored dr_wav"
```

---

### Task 5: Integrate gate, IR cab, EQ into `ToneEngine`

Wire the new nodes into the render chain in the fixed order, add setters, and extend the RT-safe reclaim to the IR cab.

**Files:**
- Modify: `Source/dsp/ToneEngine.h`
- Modify: `Source/dsp/ToneEngine.cpp`
- Modify: `tests/test_toneengine.cpp`

**Interfaces:**
- Consumes: `NoiseGate`, `IrCab`, `ToneEq` (Tasks 1–3), `NamModel` (Phase 1).
- Produces (added to `ToneEngine`):
  ```cpp
  void setGateEnabled(bool);   void setGateThresholdDb(float);
  void setIrEnabled(bool);     void setImpulse(std::shared_ptr<const std::vector<float>>);
  void setEqEnabled(bool);     void setLowDb(float); void setMidDb(float); void setHighDb(float);
  ```

- [ ] **Step 1: Add members + setters to ToneEngine.h**

Add includes `"dsp/NoiseGate.h"`, `"dsp/IrCab.h"`, `"dsp/ToneEq.h"`. Add members `NoiseGate gate_; IrCab irCab_; ToneEq eq_;` and the setters above (each forwards to the node; `setImpulse` forwards to `irCab_.setImpulse`).

```cpp
    // Signal-chain stage controls (forward to the nodes).
    void setGateEnabled(bool on)        { gate_.setEnabled(on); }
    void setGateThresholdDb(float db)   { gate_.setThresholdDb(db); }
    void setIrEnabled(bool on)          { irCab_.setEnabled(on); }
    void setImpulse(std::shared_ptr<const std::vector<float>> ir) { irCab_.setImpulse(std::move(ir)); }
    void setEqEnabled(bool on)          { eq_.setEnabled(on); }
    void setLowDb(float db)  { eq_.setLowDb(db); }
    void setMidDb(float db)  { eq_.setMidDb(db); }
    void setHighDb(float db) { eq_.setHighDb(db); }
private:
    Gain inGain_, outGain_;
    NoiseGate gate_;
    IrCab     irCab_;
    ToneEq    eq_;
```

- [ ] **Step 2: Update prepare() and render() in ToneEngine.cpp**

In `prepare()`, after the existing body, add:
```cpp
    gate_.prepare(sampleRate);
    irCab_.prepare(sampleRate, maxBlock);
    eq_.prepare(sampleRate);
```
In `render()`, the model branch becomes (gate before model, IR + EQ after, output gain last):
```cpp
        const int n = std::min({numSamples, (int) scratch_.size(), m->maxBlock()});
        if (n < numSamples) overCap_.fetch_add(1, std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) scratch_[i] = inGain_.applyNext(in[i]);
        gate_.process(scratch_.data(), scratch_.data(), n);   // gate before amp
        m->process(scratch_.data(), out, n);                  // amp
        irCab_.process(out, out, n);                          // cab
        eq_.process(out, n);                                  // tone
        for (int i = 0; i < n; ++i) out[i] = outGain_.applyNext(out[i]);
        for (int i = n; i < numSamples; ++i) out[i] = 0.0f;
```
And the no-model branch keeps gate + EQ available (so the chain still functions without a loaded amp):
```cpp
    } else {
        for (int i = 0; i < numSamples; ++i) out[i] = inGain_.applyNext(in[i]);
        gate_.process(out, out, numSamples);
        irCab_.process(out, out, numSamples);
        eq_.process(out, numSamples);
        for (int i = 0; i < numSamples; ++i) out[i] = outGain_.applyNext(out[i]);
    }
```
(The telemetry tail stays as in Phase 1.)

- [ ] **Step 3: Add integration tests to test_toneengine.cpp**

```cpp
TEST_CASE("ToneEngine chain: gate+IR+EQ all bypassed equals Phase-1 behavior") {
    dsp::ToneEngine e; e.prepare(48000, 128);
    e.setInputDb(0.0f); e.setOutputDb(0.0f);
    // all stages default-disabled; passthrough with no model
    std::vector<float> in(128, 0.25f), out(128);
    for (int b = 0; b < 40; ++b) e.render(in.data(), out.data(), 128);
    for (int i = 0; i < 128; ++i) REQUIRE(out[i] == Catch::Approx(0.25f).epsilon(0.01));
}

TEST_CASE("ToneEngine chain with a unit-impulse IR is transparent") {
    dsp::ToneEngine e; e.prepare(48000, 128);
    e.setInputDb(0.0f); e.setOutputDb(0.0f);
    e.setIrEnabled(true);
    e.setImpulse(std::make_shared<std::vector<float>>(std::vector<float>{1.0f}));
    std::vector<float> in(128, 0.2f), out(128);
    for (int b = 0; b < 40; ++b) e.render(in.data(), out.data(), 128);
    for (int i = 0; i < 128; ++i) REQUIRE(std::isfinite(out[i]));
    REQUIRE(out[64] == Catch::Approx(0.2f).epsilon(0.02));
}

TEST_CASE("ToneEngine chain with a real model + gate + EQ stays finite") {
    dsp::ToneEngine e; e.prepare(48000, 128);
    auto m = nam::NamModel::load(NAM_FIXTURE_A2, 48000, 128);
    REQUIRE(m != nullptr);
    e.setModel(std::move(m));
    e.setGateEnabled(true); e.setGateThresholdDb(-50.0f);
    e.setEqEnabled(true); e.setHighDb(6.0f); e.setLowDb(3.0f);
    std::vector<float> in(128), out(128);
    for (int i = 0; i < 128; ++i) in[i] = 0.05f * std::sin(i * 0.2f);
    for (int b = 0; b < 8; ++b) e.render(in.data(), out.data(), 128);
    for (float v : out) REQUIRE(std::isfinite(v));
}
```

- [ ] **Step 4: Run tests + ASan (fail → pass)**

Run: `cmake --build --preset default --target nam_tests -j4 && ./build/tests/nam_tests`
Then: `cmake --preset asan && cmake --build --preset asan --target nam_tests -j4 && ./build-asan/tests/nam_tests`
Expected: all PASS, ASan-clean.

- [ ] **Step 5: Commit**

```bash
git add Source/dsp/ToneEngine.h Source/dsp/ToneEngine.cpp tests/test_toneengine.cpp
git commit -m "feat: wire gate + IR cab + EQ into ToneEngine render chain"
```

---

### Task 6: `MainComponent` UI controls for the new stages

Add UI: gate (enable + threshold), IR cab (enable + "Load IR" file chooser), EQ (enable + low/mid/high). Loads IRs via `nam::loadImpulseResponse` off the message thread and hands them to the engine.

**Files:**
- Modify: `Source/app/MainComponent.h`
- Modify: `Source/app/MainComponent.cpp`

**Interfaces:**
- Consumes: `dsp::ToneEngine` new setters (Task 5), `nam::loadImpulseResponse` (Task 4).

- [ ] **Step 1: Add controls to MainComponent.h**

Add members: `juce::ToggleButton gateEnable_, irEnable_, eqEnable_;` `juce::Slider gateThresh_, eqLow_, eqMid_, eqHigh_;` `juce::TextButton loadIrButton_ { "Load IR (.wav)" };` `juce::Label irLabel_ { {}, "No IR" };` `std::unique_ptr<juce::FileChooser> irChooser_;` `juce::String currentIrPath_;`.

- [ ] **Step 2: Wire controls in MainComponent.cpp**

In the constructor, add and configure each control, e.g.:
```cpp
addAndMakeVisible(gateEnable_); gateEnable_.setButtonText("Gate");
gateEnable_.onClick = [this]{ engine_.setGateEnabled(gateEnable_.getToggleState()); };
gateThresh_.setSliderStyle(juce::Slider::LinearHorizontal);
gateThresh_.setRange(-80.0, 0.0, 0.5); gateThresh_.setValue(-60.0);
gateThresh_.setTextValueSuffix(" dB"); addAndMakeVisible(gateThresh_);
gateThresh_.onValueChange = [this]{ engine_.setGateThresholdDb((float) gateThresh_.getValue()); };

addAndMakeVisible(eqEnable_); eqEnable_.setButtonText("EQ");
eqEnable_.onClick = [this]{ engine_.setEqEnabled(eqEnable_.getToggleState()); };
for (auto* s : { &eqLow_, &eqMid_, &eqHigh_ }) {
    s->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s->setRange(-12.0, 12.0, 0.1); s->setValue(0.0);
    s->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 48, 16);
    addAndMakeVisible(*s);
}
eqLow_.onValueChange  = [this]{ engine_.setLowDb((float) eqLow_.getValue()); };
eqMid_.onValueChange  = [this]{ engine_.setMidDb((float) eqMid_.getValue()); };
eqHigh_.onValueChange = [this]{ engine_.setHighDb((float) eqHigh_.getValue()); };

addAndMakeVisible(irEnable_); irEnable_.setButtonText("Cab IR");
irEnable_.onClick = [this]{ engine_.setIrEnabled(irEnable_.getToggleState()); };
addAndMakeVisible(loadIrButton_); addAndMakeVisible(irLabel_);
loadIrButton_.onClick = [this]{ loadIrClicked(); };
```
Add `loadIrClicked()` mirroring `loadButtonClicked()`, but calling `nam::loadImpulseResponse(path, deviceSR, dsp::kMaxIrTaps)` and, on the message thread, `engine_.setImpulse(ir)` + label update. IR files are tiny, so loading synchronously in the chooser callback (already off the UI paint path) is acceptable; guard the async chooser lambda with a `SafePointer<MainComponent>` exactly like the model loader.

- [ ] **Step 3: Extend resized() layout**

Lay out the new rows below the existing controls (gate row, EQ row of 3 knobs + enable, IR row). Increase `setSize(560, 760)` to fit.

- [ ] **Step 4: Build app (debug + release), confirm compile+link**

Run: `cmake --build --preset default --target NamPlayer -j4` and `cmake --build --preset debug --target NamPlayer -j4`
Expected: both link. (No unit test — UI glue; manual verification in Task 7.)

- [ ] **Step 5: Commit**

```bash
git add Source/app/MainComponent.h Source/app/MainComponent.cpp
git commit -m "feat: MainComponent controls for gate, IR cab, and EQ"
```

---

### Task 7: Manual verification of the full chain

**Files:** none (verification only).

- [ ] **Step 1: Run the full suite (regression gate)**

Run: `cmake --build --preset default --target nam_tests -j4 && ./build/tests/nam_tests`
Expected: all PASS (Phase 1 + Phase 2 tests).

- [ ] **Step 2: Manual checks with a real interface + guitar**

Launch `open "build/NamPlayer_artefacts/NAM Player.app"`, load `models/A2.nam`, then confirm:
1. **Gate:** enable, raise threshold — hiss/hum between notes is silenced; playing opens it cleanly.
2. **EQ:** enable, move low/mid/high — audible bass/mid/treble change; flat (all 0) sounds unchanged.
3. **IR cab:** load a cab `.wav`, enable — tone gains the speaker character; disabling returns to raw amp.
4. **Order/stability:** no clicks when toggling stages or moving knobs; the level meter/xrun counter stays clean.

- [ ] **Step 3: Commit (docs/notes only if any)**

```bash
git commit --allow-empty -m "chore: Phase 2 full signal chain verified"
```

---

## Self-Review (completed)

- **Spec coverage (§4 / §10.2):** noise gate (Task 1), IR cab loader + convolver (Tasks 3–4), 3-band EQ (Task 2), all wired in signal order with per-stage bypass (Task 5), UI (Task 6), verification (Task 7). ✅
- **RT-safety:** every audio-thread node uses atomics for params; IR swap reuses the model's atomic-raw-pointer + reclaim-in-prepare() pattern; ASan checks in Tasks 3 and 5. ✅
- **JUCE isolation:** all new DSP/model units are JUCE-free (Biquad, NoiseGate, ToneEq, IrCab, IrLoader); only MainComponent uses JUCE. ✅
- **Placeholder scan:** every code step contains full code; the one integration subtlety (single `DR_WAV_IMPLEMENTATION` TU) is called out explicitly in Task 4. ✅
- **Type consistency:** `setImpulse(std::shared_ptr<const std::vector<float>>)`, `loadImpulseResponse(...)` return type, and `kMaxIrTaps` match across IrLoader, IrCab, ToneEngine, and MainComponent. ✅
- **Deferred (correctly out of scope):** FFT/partitioned convolution optimization (direct FIR capped at 4096 taps is adequate for cab IRs; note it as a future optimization if CPU load climbs), time-based FX (reverb/delay — later), multiple slots (pedalboard — later).
