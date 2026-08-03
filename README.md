# NAM Player

A cross-platform app to play guitar through [Neural Amp Modeler](https://www.neuralampmodeler.com/) (NAM) amp captures — with first-class support for the new **A2 / A2-Lite** model architecture and one-click download of tones from [TONE3000](https://www.tone3000.com/).

Plug in your guitar, load an amp model (locally or straight from TONE3000's catalog), and play — with a full practice-tone signal chain around it.

> Status: **desktop-verified end-to-end** (macOS). One C++/JUCE codebase targeting Windows, macOS, Linux, iOS, and Android.

## Features

- **A2 neural amp models** — runs `.nam` captures (A1 and the newer A2 "SlimmableContainer" format) via [`mikeoliphant/NeuralAudio`](https://github.com/mikeoliphant/NeuralAudio).
- **Full signal chain**, each stage bypassable:
  `input gain → noise gate → amp model → speaker-cab IR → 3-band EQ → output gain`
- **Speaker cab IRs** — load `.wav` impulse responses (direct-form FIR convolver).
- **Local library** — import/favorite/recent models & IRs, stored offline; survives restarts.
- **TONE3000 download** — browse TONE3000's catalog (OAuth2 **PKCE**, hosted picker), pick a tone, and its A2 model downloads straight into your library.
- **Low-latency audio** — native audio backends per OS (CoreAudio / ASIO / ALSA / Oboe) with a device/buffer picker, a live latency readout, and a lock-free level/CPU/xrun meter.

## Architecture

The codebase is split into a **JUCE-free, headlessly unit-tested core** and a thin **JUCE shell**:

| Layer | Location | JUCE? | Tested |
|-------|----------|-------|--------|
| DSP (engine, gate, EQ, IR cab, gains) | `Source/dsp` | no | ✅ |
| Model & library (NAM wrapper, loaders, library store) | `Source/model` | no | ✅ |
| Networking (PKCE, TONE3000 API) | `Source/net` | no | ✅ |
| App shell (audio I/O, UI, OAuth flow, download) | `Source/app` | yes | build + manual |

The **#1 invariant**: the real-time audio callback never allocates, locks, throws, or does I/O — model/IR hand-offs use a lock-free atomic-pointer + control-thread-owned-lifetime pattern. Keeping DSP/model/net JUCE-free means the hard parts are portable (e.g. a future WebAssembly build) and covered by ~70 headless test cases.

## Build

Requires CMake ≥ 3.22 and a C++17 compiler. JUCE and Catch2 are fetched automatically; NeuralAudio is a submodule.

```bash
git clone --recurse-submodules https://github.com/cloudcodetree/nam-app.git
cd nam-app
# (if you cloned without --recurse-submodules)
git submodule update --init --recursive

# Build + run the tests (headless, no audio hardware needed)
cmake --preset default
cmake --build --preset default --target nam_tests
./build/tests/nam_tests

# Build the app
cmake --build --preset default --target NamPlayer
# macOS bundle: build/NamPlayer_artefacts/Release/NAM Player.app
```

Presets: `default` (Release), `debug`, `asan`, `tsan` (see `CMakePresets.json`).

### TONE3000 credentials (optional)

The in-app TONE3000 download needs a **publishable key** (`t3k_pub_…`) from your TONE3000 account. Copy `.env.example` to `.env` and fill it in:

```bash
cp .env.example .env
# set TONE3000_PUBLISHABLE_KEY=t3k_pub_...
```

`.env` is gitignored. The publishable key is the only credential used — it's a public OAuth2 **PKCE** client id (safe to embed). The client secret is never read or compiled in.

## Roadmap

- [x] Phase 1 — audio engine + device picker + local `.nam` load
- [x] Phase 2 — full signal chain (gate, IR cab, EQ)
- [x] Phase 3 — local library (favorites, recents, offline)
- [x] Phase 4a — TONE3000 download (OAuth2 PKCE, `select_tone`)
- [ ] Phase 4b — in-app TONE3000 search browser
- [ ] Phase 5 — iOS + Android packaging
- [ ] Phase 6 — Web / PWA build (reuse the JUCE-free core via WebAssembly)

## Credits & dependencies

- [Neural Amp Modeler](https://github.com/sdatkinson/neural-amp-modeler) — Steven Atkinson
- [NeuralAudio](https://github.com/mikeoliphant/NeuralAudio) (MIT) — real-time NAM inference
- [TONE3000](https://www.tone3000.com/) — tone catalog + API
- [JUCE](https://juce.com/) — cross-platform app/audio framework
- [dr_wav](https://github.com/mackron/dr_libs) (public domain), [PicoSHA2](https://github.com/okdshin/PicoSHA2) (MIT), [nlohmann/json](https://github.com/nlohmann/json) (MIT)

## License

Licensed under the **GNU General Public License v3.0** — see [`LICENSE`](LICENSE).

This is the standard open-source path for a [JUCE](https://juce.com/) application: JUCE is dual-licensed (GPLv3 **or** a paid commercial license), and using it for free requires the resulting app to be GPLv3. All other dependencies (NeuralAudio MIT, dr_wav public-domain, PicoSHA2 MIT, nlohmann/json MIT) are GPL-compatible.
