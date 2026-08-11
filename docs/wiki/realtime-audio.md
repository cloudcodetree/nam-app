# Real-time audio

## The law

Nothing on the audio thread (`getNextAudioBlock`, `ToneEngine::render`, and
everything they call) allocates, locks, throws, logs, or does I/O.

## Patterns in use (learned the hard way)

- **Publish-then-retire with RAW atomic pointers.** shared_ptr owners live
  on the message thread (`demoTracks_`/`demoSlots_` + `retiredDemos_`); the
  audio thread reads `std::atomic<const T*>` (`demoTracksRT_`/`demoSlotsRT_`).
  DO NOT use `std::atomic_load(shared_ptr*)` on the audio thread — libc++
  implements it with a mutex pool (reviewer-confirmed BLOCKER).
- **Block-gated reclamation.** A retired buffer/model frees only after TWO
  device blocks complete post-retirement (`appBlocks_` in the host,
  `blockCount_` in ToneEngine), because several publishes can flush in one
  message-loop drain with no block boundary between them. The block counter
  increments with `memory_order_release` and reclamation loads it with
  `acquire` — without that edge the whole safety argument is void.
- **Write the whole device block.** Loops bound by `n` AND `cap`; the tail
  `[cap, n)` is zeroed — on a duplex stream unwritten output samples are RAW
  MIC INPUT and bypass every mute.
- **Atomics**: `relaxed` for telemetry, `acquire/release` for pointer
  hand-off. Output metering taps the device write, not the engine (engine
  telemetry freezes when bypassed).

## Mute semantics

- Feedback guard (no USB interface): mutes the live INPUT path only; raw
  mic still meters, tuner stays usable, demos play through the speaker.
- User input mute silences the tuner too. Output mute is user-only.
- Demo play lifts an output mute; demo stop re-mutes input when on the
  system mic. TEST TONE (orb flyout) is a 440 Hz sine added post-chain,
  envelope-ramped, gated by an atomic flag; stops when the panel closes.

## Devices / latency (Android)

- Buffer default was 1920 frames (~40 ms) — clamping to the device minimum
  (192) was the real latency fix (~14–16 ms round trip).
- Explicit USB claim of BOTH sides beats "System Default" routing: immune to
  Bluetooth hijack (Ray-Bans), at the cost of Legacy path vs MMAP.
- AAudio streams don't auto-reroute; the watchdog rescans after ~2 s of a
  dead device. `prepareToPlay` is the only always-safe reclaim point.
