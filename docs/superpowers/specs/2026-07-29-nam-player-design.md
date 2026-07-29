# Design: NAM Player — a cross-platform Neural Amp Modeler tone player

**Date:** 2026-07-29
**Status:** Approved design, ready for implementation planning

## 1. Goal & Scope

A single application, built from one C++/JUCE codebase, that runs on **Windows,
macOS, Linux, iOS, and Android**. The user plugs in a guitar, browses and
downloads amp models from **TONE3000**, loads one into an amp slot, and plays
through it live with low latency.

**v1 is a discovery-first single-amp player** — not a full pedalboard. The
headline experience is: find a tone on TONE3000, load it, and play. A complete
but single-instance signal chain wraps the model so the tone is actually usable.

### Explicitly out of scope for v1
- Multiple simultaneous amp/pedal slots (pedalboard).
- Time-based effects (reverb, delay, modulation).
- Plugin export (AUv3/VST3). JUCE keeps this door open for a later version, but
  v1 ships standalone only.
- Tone capture/training (this app plays models; it does not create them).

## 2. Target Platforms & Tech Stack

- **Framework:** JUCE (C++) — single codebase to all 5 targets, native
  low-latency audio backends on each OS (ASIO/WASAPI on Windows, CoreAudio on
  macOS/iOS, ALSA/JACK on Linux, Oboe/AAudio on Android).
- **Neural inference:** [mikeoliphant/NeuralAudio](https://github.com/mikeoliphant/NeuralAudio)
  (MIT license) — provides NAM A1 and **A2 / A2-Lite** support with optimized
  static implementations, embedded as a submodule.
- **Build system:** CMake.

### Why JUCE
Latency and DSP performance are dominated by the OS audio API + buffer size +
interface hardware, not the UI framework. JUCE rides each platform's lowest-
latency native audio path, so it is tied for best real-time performance while
being the only option where "5 platforms + real-time audio + reuse the C++ NAM
engine" is the default rather than a fight.

## 3. Architecture

Two layers plus a services tier, with one critical real-time boundary.

```
┌──────────────────────────────────────────────┐
│  UI Layer (JUCE Components)                    │
│  • Tone browser (TONE3000)  • Library          │
│  • Amp slot / signal-chain controls            │
│  • Settings: audio device + buffer picker      │
└───────────────┬───────────────────────────────┘
                │  thread-safe param messages (lock-free)
┌───────────────▼───────────────────────────────┐
│  Audio Engine (real-time C++, no allocations)  │
│  In gain → Gate → NAM(A2) → IR cab → EQ → Out  │
│  NeuralAudio (MIT) does A2/A2-Lite inference    │
└───────────────┬───────────────────────────────┘
                │
┌───────────────▼───────────────────────────────┐
│  Services (non-realtime)                        │
│  • TONE3000 client (OAuth2+PKCE, search, DL)    │
│  • Local library store (models + IRs + meta)    │
│  • Model/IR file loaders                         │
└──────────────────────────────────────────────┘
```

### The critical boundary: UI thread ↔ audio thread
The audio thread **never allocates, never locks, and never does I/O**. All
parameter changes flow from the UI through a lock-free queue. Model and IR swaps
are fully prepared off-thread, then handed to the audio thread via an atomic
pointer exchange. This single discipline is what keeps a real-time guitar app
glitch-free and is the highest-priority invariant in the codebase.

## 4. Components

Each component is independently understandable and testable, with a clear
interface and explicit dependencies.

| Component | Responsibility | Depends on |
|-----------|----------------|------------|
| `AudioEngine` | Owns the processing chain; `prepare()`, `process(buffer)`. Pure DSP. | Chain nodes |
| `InputGain` | Level staging before the amp. | — |
| `NoiseGate` | Threshold gate before the amp for high-gain hum/hiss. | — |
| `NamModel` | Wraps NeuralAudio; runs A1/A2/A2-Lite inference. | NeuralAudio |
| `IrCab` | Partitioned convolution of a speaker cabinet impulse response. | — |
| `ToneEq` | 3-band / tilt EQ shaping the final sound. | — |
| `OutputLevel` | Final output gain. | — |
| `ModelHost` | Loads `.nam`/binary models off-thread, hot-swaps atomically. | NamModel, file loaders |
| `Tone3000Client` | OAuth2/PKCE auth, search, model + IR download. No UI. | HTTP (JUCE) |
| `Library` | Local store of downloaded models/IRs; favorites, recents, offline play. | Filesystem |
| `AudioDeviceController` | Wraps JUCE device manager; lists devices/buffer sizes; latency + Bluetooth warnings. | JUCE device manager |
| `BrowserView` / `LibraryView` / `AmpSlotView` / `SettingsView` | UI. | Services + engine params |

Each chain node exposes a uniform interface (`prepare`, `process`, `reset`,
bypass), so the chain is a list of interchangeable nodes.

## 5. Signal Flow

```
Guitar → interface → Input gain → Noise gate → NAM model (A2) → IR cab → EQ → Output level → headphones/monitors
```

- Every stage is individually bypassable.
- The IR cab defaults to off/neutral so that amp+cab captures are not
  double-cabbed. The user opts into loading a separate cab IR.

## 6. Audio I/O Strategy

- JUCE device manager on each OS's low-latency backend.
- A **device + buffer-size picker** with a live round-trip latency readout.
- Recommend a USB/USB-C audio interface as the primary path.
- Support system-default device, headphone-jack/built-in-mic fallback, and
  Bluetooth — but **warn in-app** that Bluetooth (and the built-in mic) add
  latency unsuitable for playing along.
- Default buffer 128 samples at 48 kHz, user-adjustable.

## 7. TONE3000 Integration

- **Auth:** OAuth 2.0 with PKCE (the standard native-app flow). Requires
  registering the app / obtaining a client ID and redirect URI with TONE3000.
- **Capabilities:** search/browse the catalog, download model files and IRs,
  sync the user's library. Reference: <https://github.com/tone-3000/api>.
- **Local library:** downloaded models and IRs are stored locally with metadata,
  favorites, and recents, so the app plays fully **offline** once content is
  downloaded.

## 8. Error Handling

- **Audio device drop/unplug:** engine goes silent and stays safe; the audio
  thread never crashes. UI shows a reconnect banner.
- **Model load failure** (corrupt/unsupported architecture): keep the previously
  loaded model; surface a clear error message.
- **Network failure** (TONE3000 down / auth expired): the local library still
  works offline; the browser shows a retry state.
- **Architecture detection:** detect A1 vs A2 (and sub-variant) from the file; if
  a model requires an engine capability not present, fail gracefully with a
  message rather than producing bad audio.

## 9. Testing Strategy

- **Unit tests:** each chain node (gate threshold behavior, gain math, EQ
  response curve), the library store, and the TONE3000 client (mocked HTTP).
- **Golden-file DSP tests:** feed a known WAV through a known model and assert the
  output matches a stored reference within tolerance — catches inference
  regressions.
- **No-allocation check:** assert the audio callback performs zero heap
  allocations.
- **Manual latency verification** per platform with a real audio interface.

## 10. Build & Sequencing

JUCE + CMake, NeuralAudio as a submodule. Suggested implementation order:

1. Audio engine skeleton + device/buffer picker + local `.nam` load → **hear a
   tone end-to-end**.
2. Full single-amp signal chain (gate, IR cab, EQ, level staging).
3. Local library store (downloaded models/IRs, favorites, recents, offline).
4. TONE3000 OAuth + browse/download integration.
5. Per-platform packaging: desktop (Win/macOS/Linux) first, then iOS, then
   Android.

## 11. Dependencies to Verify During Planning

- NeuralAudio's exact A2 / A2-Lite API surface and its mobile (iOS/Android) build
  flags and vectorization requirements.
- TONE3000 OAuth client registration: whether a registered client ID / redirect
  URI is required, and any rate limits or terms for third-party apps.

Both are assessed as low-risk but will be confirmed before their respective
implementation phases.
