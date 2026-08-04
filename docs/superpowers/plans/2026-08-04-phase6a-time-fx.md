# NAM Player — Phase 6a: Time-Based FX (Delay + Reverb)

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. Steps use `- [ ]` checkboxes.

**Goal:** Add **Delay** and **Reverb** as bypassable post-amp effect stages — the first step toward the full pedalboard. Each is a JUCE-free, RT-safe, unit-tested DSP node (same proven pattern as `NoiseGate`/`ToneEq`), wired into `ToneEngine` and the UI.

**Signal order (new):** `input gain → gate → NAM model → IR cab → EQ → Delay → Reverb → output gain`

## Global Constraints

- C++17. `Source/dsp/` nodes MUST NOT include JUCE.
- **RT-safety (#1 invariant):** `process()` on the audio thread — no heap alloc, no lock, no throw, no I/O. Preallocate all buffers in `prepare()`. Params via `std::atomic` (relaxed).
- Recursive/feedback state (delay feedback, reverb combs/allpasses) MUST flush denormals (same as `Gain`/`NoiseGate`/`ToneEq`).
- Every stage bypassable; disabled = transparent passthrough (in-place safe).
- Mono, 32-bit float, default 48000 Hz.

## File Structure

```
Source/dsp/Delay.h            # header-only circular-buffer delay (JUCE-free)
Source/dsp/Reverb.h .cpp      # Freeverb-style comb+allpass reverb (JUCE-free)
Source/dsp/ToneEngine.h .cpp  # MODIFIED: delay + reverb stages + setters
Source/app/MainComponent.h/.cpp # MODIFIED: Delay + Reverb controls
tests/test_delay.cpp  test_reverb.cpp  test_toneengine.cpp (MODIFIED)
```

---

### Task 1: `Delay` node (JUCE-free, header-only, tested)

**Files:** create `Source/dsp/Delay.h`, `tests/test_delay.cpp`; modify `tests/CMakeLists.txt`.

**Interface:**
```cpp
namespace dsp {
class Delay {
public:
    void prepare(int sampleRate, int /*maxBlock*/);   // sizes the line to kMaxDelaySec
    void setEnabled(bool on)        { enabled_.store(on, std::memory_order_relaxed); }
    void setTimeMs(float ms)        { timeMs_.store(ms, std::memory_order_relaxed); }
    void setFeedback(float f)       { feedback_.store(f, std::memory_order_relaxed); }  // 0..0.95
    void setMix(float m)            { mix_.store(m, std::memory_order_relaxed); }        // 0..1 wet
    void process(const float* in, float* out, int numSamples);   // in-place safe
};
}
```

- [ ] **Step 1: Write Delay.h**
  - `kMaxDelaySec = 2.0f`. In `prepare()`: `buf_.assign((size_t)(kMaxDelaySec*sr)+1, 0.0f); writePos_=0; sr_=sr;`.
  - `process`: read `delaySamples = clamp(timeMs/1000*sr_, 1, buf_.size()-1)`; per sample: `float d = buf_[readPos]` where `readPos = (writePos_ - delaySamples + N) % N`; `out[i] = (1-mix)*in[i] + mix*d`; write `buf_[writePos_] = in[i] + feedback*d` (clamp feedback ≤ 0.95); flush denormal on the written value; advance `writePos_`. Disabled → passthrough (copy if in!=out).
  - Denormal: `if (std::fabs(w) < 1e-15f) w = 0` before storing.

- [ ] **Step 2: Tests** (`tests/test_delay.cpp`):
  - Disabled = passthrough.
  - Unit impulse, time=10ms, feedback=0, mix=1 → output has the impulse delayed by ~`0.010*sr` samples (assert a spike there, ~0 before).
  - feedback=0.5 → multiple decaying echoes (assert a second echo at 2× delay with ~half amplitude).
  - mix=0 → dry only (== input).
  - Long run of silence stays finite (denormal check).

- [ ] **Step 3:** wire `test_delay.cpp` into CMake; run tests + full suite. Commit `feat: Delay effect node (circular buffer, feedback, mix)`.

---

### Task 2: `Reverb` node (JUCE-free, Freeverb-style, tested)

**Files:** create `Source/dsp/Reverb.h/.cpp`, `tests/test_reverb.cpp`; modify `tests/CMakeLists.txt`.

**Interface:**
```cpp
namespace dsp {
class Reverb {
public:
    void prepare(int sampleRate, int /*maxBlock*/);
    void setEnabled(bool on)   { enabled_.store(on, std::memory_order_relaxed); }
    void setRoomSize(float r)  { roomSize_.store(r, std::memory_order_relaxed); }  // 0..1
    void setDamping(float d)   { damping_.store(d, std::memory_order_relaxed); }   // 0..1
    void setMix(float m)       { mix_.store(m, std::memory_order_relaxed); }       // 0..1 wet
    void process(const float* in, float* out, int numSamples);   // in-place safe
private: /* 8 comb + 4 allpass mono, preallocated */
};
}
```

- [ ] **Step 1: Implement Reverb.h/.cpp** — a standard Freeverb (mono): 8 lowpass-feedback comb filters (tunings ~1116,1188,1277,1356,1422,1491,1557,1617 samples, scaled to sr) summed, then 4 allpass filters (tunings ~556,441,341,225) in series. `roomSize`→comb feedback (0.7..0.98), `damping`→comb lowpass. Preallocate all comb/allpass buffers in `prepare()`. Flush denormals on all feedback state. `mix` blends dry/wet. Disabled → passthrough.

- [ ] **Step 2: Tests** (`tests/test_reverb.cpp`):
  - Disabled = passthrough.
  - mix=0 → dry only.
  - Impulse in, mix=1 → output finite, non-zero decaying tail extends well past the impulse (assert energy present hundreds of ms later; larger `roomSize` → longer/louder tail).
  - Long silence → finite, decays toward 0 (denormal check).

- [ ] **Step 3:** wire into CMake; run tests + full suite + ASan (`cmake --preset asan && ... nam_tests`). Commit `feat: Reverb effect node (Freeverb-style comb/allpass)`.

---

### Task 3: Integrate Delay + Reverb into `ToneEngine`

**Files:** modify `Source/dsp/ToneEngine.h/.cpp`, `tests/test_toneengine.cpp`.

- [ ] Add members `Delay delay_; Reverb reverb_;` + setters (`setDelayEnabled/setDelayTimeMs/setDelayFeedback/setDelayMix`, `setReverbEnabled/setReverbRoomSize/setReverbDamping/setReverbMix`). In `prepare()` call `delay_.prepare(sr,maxBlock); reverb_.prepare(sr,maxBlock);`. In `render()`, AFTER `eq_.process(out,n)` and BEFORE the output-gain loop, add `delay_.process(out,out,n); reverb_.process(out,out,n);` (model branch on `n`; no-model branch on `numSamples`). Keep telemetry tail + bounds clamp intact.
- [ ] Add an integration test: full chain with delay+reverb enabled + a real model stays finite; all-bypassed still equals the pre-6a behavior.
- [ ] Run full suite + ASan. Commit `feat: wire Delay + Reverb into ToneEngine chain`.

---

### Task 4: UI controls + finish

**Files:** modify `Source/app/MainComponent.h/.cpp`.

- [ ] Add Delay controls (enable toggle + time/feedback/mix sliders) and Reverb controls (enable + roomSize/damping/mix), wired to the engine setters, laid out in `resized()` (grow window if needed). Mirror the existing gate/EQ control pattern.
- [ ] Both app presets link + full suite passes. VERIFY the app link explicitly. Commit `feat: Delay + Reverb UI controls`.
- [ ] Manual: load a model, enable delay (hear echoes) + reverb (hear tail). Whole-branch review, then merge.

---

## Self-Review
- **Scope:** two post-amp time FX as bypassable stages; full reorderable pedalboard graph + drive/mod/comp nodes deferred to later 6x. ✅
- **Pattern reuse:** identical to Phase 2 nodes (atomic params, denormal flush, prepare-allocate, in-place process, ToneEngine wiring, UI). ✅
- **RT-safety:** preallocated delay line + reverb buffers; no audio-thread alloc; denormal flush on all feedback state; ASan gate on the reverb + integration. ✅
- **Testability:** delay (impulse delay + echo), reverb (decaying tail), both JUCE-free. ✅
