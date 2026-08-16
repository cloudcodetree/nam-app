# Foot controllers & rig hardware (future feature — direction locked)

**Decision (2026-08-13):** NAM Player will support **multiple controllers**
through one transport-agnostic control layer. Not yet scheduled; this spoke
holds the architecture direction + hardware research so the eventual spec
starts warm.

## Status (2026-08-16)

The control layer EXISTS as of this date, in two pieces:
`Source/model/ControlMap.{h,cpp}` (JUCE-free, unit-tested in
`tests/test_control_map.cpp`) and `Source/app/MidiControl.{h,cpp}` (the MIDI
transport: opens every USB/BLE input, normalizes to `ControlEvent`, drains on
the message thread via `AsyncUpdater`). Bindings persist to
`<appdata>/NAM Player/controls.json`. Still missing: the Controllers UI
(pair / live monitor / learn) and the dispatch that makes actions do
anything. Nothing is user-reachable yet.

Key implementation facts, so they are not re-derived:
- JUCE's Android BLE MIDI support is complete -- scan filtered on the BLE
  MIDI service UUID plus `MidiManager` pairing, exposed as
  `BluetoothMidiDevicePairingDialogue` (juce_audio_utils, already linked).
- **In-app pairing is mandatory on Android** -- a BLE MIDI device paired from
  system settings has no MIDI ports until an app calls
  `openBluetoothDevice()`.
- MIDI callbacks arrive on a JUCE MIDI thread, NOT the message or audio
  thread; everything hops before touching UI or engine.
- `FirePolicy` resolves the momentary/toggle ambiguity -- see decisions.md.

## Architecture direction

- One internal **control-event layer**: sources emit normalized events
  (button N pressed, continuous controller K = 0..1); the app maps events to
  actions/parameters via **MIDI-learn-style binding** ("press the switch you
  want for next-tone", "rock the pedal"). Multiple sources may be active at
  once (e.g. Chocolate Plus + Spark Control).
- **Pluggable transports**, in build order:
  1. **BLE MIDI + USB MIDI** — Android's MIDI API serves both; JUCE wraps
     it. Standard, durable, works with every MIDI pedal. Build first.
  2. **Spark Control adapter** — Positive Grid pedals speak PROPRIETARY BLE
     (service `FFC8`, notify `FFCA`; `0x03` = button, `0x0c` = expression),
     fully reverse-engineered at github.com/paulhamsh/Spark-Control-X.
     Needs a custom GATT client (Java in JuceActivity + JNI — generic BLE
     is outside JUCE/Android-MIDI). Label as community-protocol support;
     may break on vendor firmware updates. Chris owns a Spark controller.
  3. **BLE HID (page-turner) mode** — pedals that pair as keyboards; key
     events via JUCE. Cheap breadth for the store release, lowest priority.
- **App-side calibration beats vendor apps**: bind whatever the pedal sends
  (factory defaults suffice), auto-calibrate expression range in-app
  ("rock through full range"), invert in the mapping. Goal: users never
  install CubeSuite/etc. Optional stretch: direct pedal config via
  reverse-engineered SysEx (partial: github.com/cbix/mvave-chocolate-sysex;
  config only — NEVER firmware ops).
- Controller support is **app-native** → legitimately Pro (parity rule).

## Hardware facts (researched 2026-08-12/13)

- **M-Vave Chocolate Plus (~$50)** — first hardware target, and **Chris now
  owns one (2026-08-15)**. Full device reference, including BLE pairing name
  (`FootCtrl`), operating modes, and which "factory defaults" are actually
  contested across sources: **[chocolate-plus.md](chocolate-plus.md)**.
  Short version: 4 switches (+2 combinations), BLE MIDI + USB device/host +
  3.5 mm TRS that doubles as the expression input, PC/CC/Note/SysEx. Pair
  with M-Audio EX-P (~$20); the Plus's 3.5 mm jack is pickier about
  expression pedals than the base Chocolate's 1/4". Base Chocolate lacks the
  exp jack entirely.
- **Spark Control / Control X** — proprietary BLE (see above). Control X:
  6 switches + 2 expression inputs; original: 4 switches, no exp.
- Others w/ exp inputs: Hotone Ampero Control (~$89), XSONIC AIRSTEP
  ($279, also does HID), iRig BlueBoard (~$100, aging).
- **Expression pedals are passive pots**; the controller digitizes them to
  CC. All smarts live in the controller/our mapping.

## Power / hub facts (same research)

- **iRig HD X: bus-powered, NO passthrough charging on USB-C** (IK FAQ) —
  it drains the phone and blocks the port. Fix: **powered PD hub** (wall →
  hub: charges phone, powers iRig, frees ports for wired pedal).
- Verified candidates: SmallRig 4-in-1 (~$28, 2× USB-C data — iRig's own
  cable fits), UGREEN 5-in-1 ($13, 3× USB-A — needs A→C cables), Satechi
  Mobile Hub (~$60, 2× USB-A 10Gbps + HDMI + 3.5mm headset jack — that
  jack is a headset codec, NEVER a guitar input: impedance/level/bias all
  wrong; the iRig's Hi-Z preamp is unreplaceable by any hub).
- On hub arrival: verify USB auto-claim still matches the iRig (names can
  shift behind hubs — harden matcher if needed), re-check latency, confirm
  Chocolate USB-MIDI class compliance if wired. A hub codec enumerating as
  a USB audio device must not steal routing (explicit claim should win).
- KE1-class ($80 multi-FX pedals w/ OTG): competitors, not accessories —
  processed-signal USB, no dry-DI guarantee, unspecified latency, switches
  don't transmit MIDI. Useful only as market intel.
