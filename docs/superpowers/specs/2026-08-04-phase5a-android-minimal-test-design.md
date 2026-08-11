# NAM Player — Phase 5a: Minimal Android Audio Test (iRig HD X validation)

**Date:** 2026-08-04
**Status:** Approved design, pending implementation plan
**Branch:** `phase5a-android`

## Goal

Get the **smallest useful NAM Player build running on a Samsung Galaxy S24 Ultra** so we can
**validate the iRig HD X USB-C audio interface** end to end: guitar → interface → phone → NAM
processing → phone → output, at usable real-time latency. This is the first step of the mobile
pivot (the broader Phase 5 platform port), scoped deliberately small to de-risk the two real
unknowns before investing in a full UI/library/OAuth port.

## Why "minimal first" (scope decision)

The user's immediate objective is to **test the iRig hardware on the phone**, not to ship the full
app. A minimal test app reaches that objective with the least yak-shaving and isolates the two
things that can actually go wrong on Android, so we learn the answers cheaply:

1. **Does the JUCE-free core cross-compile for `arm64-v8a`?** — `Source/dsp`, `Source/model`, and
   the NeuralAudio submodule (with Eigen + RTNeural) have never been built for Android.
2. **Does Android / Samsung OneUI route the class-compliant iRig HD X through JUCE's audio layer
   at low latency?** — the actual prize, and the actual risk.

Everything not needed to answer those two questions is **out of scope for 5a** (see Non-Goals).

## Build architecture (the load-bearing decision)

**Chosen: Approach A — a thin `Builds/Android` Gradle project whose `externalNativeBuild` invokes
our existing `CMakeLists.txt`.** Gradle supplies the Android app shell (manifest, JUCE's Java
`JuceActivity` + module Java sources, resource packaging, APK assembly) and delegates *all* native
compilation to our CMake, which JUCE already supports when `CMAKE_SYSTEM_NAME STREQUAL "Android"`.

Verified in the fetched JUCE 8.0.15 tree: `extras/Build/CMake/JUCEModuleSupport.cmake` already
handles Android (`JUCE_ANDROID=1`, links `android`/`log`, pulls NDK `cpu-features`, wires **Oboe**
into `juce_audio_devices`). So JUCE's *native library* builds via CMake for Android; only the app
*shell* (manifest / Java activity / packaging) is not emitted by the CMake API — that is what the
Gradle wrapper provides.

| Approach | Pros | Cons |
|---|---|---|
| **A. Gradle wrapper → our CMake** (chosen) | One native build definition across all platforms; CMake stays the single source of truth; Android is just a platform wrapper | Hand-author the small Gradle + manifest shell once (crib from a JUCE example's `Builds/Android/`) |
| B. Projucer-generated Android Studio project | JUCE-blessed; generates the whole project | Must build/run Projucer and maintain a parallel `.jucer` duplicating the CMake target list — two sources of truth, the exact thing that hurts a cross-platform product |

Approach A keeps the cross-platform portability story intact: the JUCE-free core and the CMake
build are unchanged; the desktop presets keep working; Android is additive.

## What the minimal APK contains

Smallest thing that exercises the real audio path:

- **Signal path:** guitar in → `dsp::ToneEngine` (one **bundled** `.nam` model + the existing
  gate / EQ / delay / reverb stages) → out. Reuses the JUCE-free core verbatim.
- **Model:** one `.nam` file shipped in the APK `assets/`, copied to app-internal storage on first
  launch and loaded via the existing `NamModel::load` path (no library, no downloads).
- **UI:** minimal touch screen — audio device / connection status, **measured round-trip latency**,
  input & output gain, and a master enable. Enough to see the iRig is live and read latency.
- **Permissions/features:** `RECORD_AUDIO` (runtime request) and `android.hardware.usb.host`.

## Risks and how we validate them

1. **arm64 cross-compile of the core + NeuralAudio.** Eigen is header-only (✓) and RTNeural is
   template/header (✓), so it is *likely* clean, but unproven on Android. Mitigation: the Android
   CMake configure/build flushes this out immediately; fix any `arm64-v8a` compile issues in the
   submodule's consumption (not by forking the submodule if avoidable).
2. **USB-audio routing + latency (the real unknown).** iRig HD X is marketed Android-compatible
   over USB-C; JUCE routes audio through **Oboe/AAudio**. Open questions: does Oboe select the USB
   device for **input** (Samsung routing is historically finicky), and what buffer size / sample
   rate gives usable latency. Mitigation: this is exactly why we test with a throwaway-small app
   before the full port; iterate on AAudio sharing mode, sample rate, and buffer size on-device
   using `logcat` + the latency readout.

## Implementation sequence

1. **Toolchain install** — JDK 17, Android Studio + SDK (API 34/35), NDK (r26/r27) + CMake,
   platform-tools (adb); enable USB debugging on the S24 Ultra.
2. **Android build bring-up** — author `Builds/Android` (Gradle + manifest + JUCE Java glue),
   wire `externalNativeBuild` to the existing `CMakeLists.txt`; get the JUCE-free core + a trivial
   JUCE audio component compiling for `arm64-v8a` and launching on the device.
3. **Wire the audio path** — bundle one `.nam` in `assets/`, run `ToneEngine` in the audio
   callback, add the minimal status/latency/gain UI.
4. **On-device iRig validation** — deploy, confirm the iRig is selected for I/O, **measure
   round-trip latency**, iterate on buffer/sample-rate/AAudio settings; record findings.

## Non-Goals (deferred to the full Phase 5 port)

- TONE3000 browse / search / download and the **mobile OAuth redirect** rework (the desktop
  `127.0.0.1` loopback does not apply on mobile) — the minimal app has no network/auth at all.
- Local library panel, favorites/recents, importer, file-picker flows.
- Full touch UI / responsive layout of the desktop controls.
- iOS (the user's test device is Android; iOS is a separate later step).
- CI for Android (dev-machine build first; headless CI comes later).
- Play Store packaging / signing for distribution.

## Success criteria

- The JUCE-free core + NeuralAudio compile and link for `arm64-v8a`.
- The APK installs and launches on the S24 Ultra.
- With the iRig HD X connected over USB-C, the app processes guitar through the bundled NAM model
  in real time, and we can **read a concrete round-trip latency figure** and judge whether it is
  usable — the answer that unblocks the rest of the mobile pivot.
