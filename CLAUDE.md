# NAM Player — coding standards

These rules bind every change (human or agent). The pre-push adversarial
reviewer treats violations in new/changed code as findings: rule breaks that
can corrupt audio, leak data, or crash are BLOCKER; structural breaks (file
growth, god functions, unregistered sources) are MAJOR.

## Structure — no god files

- **New files: ≤ 400 lines.** A screen, overlay, service, or DSP node that
  wants more is two components — extract before it lands.
- **Files already over 800 lines must not grow.** A change to
  `AppShell.cpp`, `AndroidAudioApp.cpp`, or `PlayScreen.cpp` that adds a new
  feature extracts it (own .h/.cpp, registered in CMake) instead of inlining
  another few hundred lines. Shrinking oversized files opportunistically is
  encouraged; bundling unrelated refactors is not.
- **Functions ≤ ~60 lines, one responsibility.** Paint helpers, hit-test
  blocks, and service lambdas that outgrow this become named members.
- Every new `.cpp` is registered in **every CMake target that links it**
  (app target and tests target where applicable). A compiling header proves
  nothing about linking.

## Real-time audio (non-negotiable)

- Nothing on the audio thread (`getNextAudioBlock`, `ToneEngine::render`,
  anything they call) allocates, locks, throws, logs, or does I/O.
- Cross-thread buffers use **publish-then-retire**: writers publish a new
  immutable `shared_ptr` (message thread); the audio thread takes its own
  reference at block start. Never mutate a container the audio thread can be
  reading (`demoTracks_`/`demoSlots_`/`cabIrs_` are the pattern).
- Atomics: `relaxed` for telemetry values, `acquire/release` for pointer
  hand-off. Pre-size all buffers in `prepareToPlay`; bound every audio-thread
  loop by both the block size and the buffer capacity, and write (or zero)
  the **whole** device block — no untouched tail.
- Anything unbounded gets a cap and pruning (model cache, artwork cache,
  retired models, audition cache).

## Async + UI correctness

- Every `std::function` service call is null-checked.
- An async callback **re-validates the world it captured**: index still in
  range AND still the same id/tone (a search, filter, or deck change may have
  replaced the rows). Prefer capturing ids over indices.
- Background → UI hops go through `MessageManager::callAsync`; prefer
  `Component::SafePointer` over bare `this` when the target can die first.
- Expensive resources (sessions, threads) are reused, not rebuilt per call —
  destroying one must never block the message thread.

## UI house rules

- Every flyout/dropdown/overlay is **height-capped and scrollable**, never
  full screen; overlays paint LAST in `paint()`.
- Non-ASCII glyphs only via `juce::String::fromUTF8`; prefer vector
  `juce::Path` icons (Android colour-emoji fallback breaks exotic glyphs).
- Colours come from `nam::ui::col`; no magic hex outside `NamLookAndFeel.h`.
- Touch follows the press/drag/tap state machine; new hit rects must not
  shadow existing gestures, and hit tests must guard against empty rects
  (`empty.expanded(k)` creates a live region near the origin).

## Security

- `TONE3000_CLIENT_SECRET` (`t3k_cs_`) is never read, compiled, or logged;
  only the publishable key (`t3k_pub_`) may be embedded.
- OAuth tokens/codes/verifiers never appear in logs. Token storage is
  reported by existence/size only.
- API-supplied strings are validated before becoming paths (charset
  whitelist, no traversal) or fetch targets (https only); network reads are
  size-bounded.

## Process

- **clang-format is law**: `.clang-format` at the root (compact core style)
  with a JUCE-spacing override in `Source/app/ui/.clang-format`. The
  pre-push hook blocks unformatted `Source/` files — run
  `clang-format -i <files>` before committing. `.clang-tidy` runs advisory
  in the hook (bugprone/concurrency/performance/function-size).
- Android native builds use **RelWithDebInfo** (a -O0 NAM is 5–15× too slow
  for real time).
- **TDD in the JUCE-free core** (`Source/dsp`, `Source/model`, `Source/net`):
  write the failing test FIRST, then the code that makes it pass. Every
  behavior change lands with its test in the same commit; the headless suite
  (`tests/`, both CMake targets) stays green. Bug fixes start with a test
  that reproduces the bug.
- **E2E for user-visible flows**: a feature isn't done until it's exercised
  on a real target — emulator screenshots for UI flows, the phone for
  audio-path/latency changes. The JUCE UI layer has no unit harness, so
  device verification IS its test; record what was verified in the commit.
- **The wiki rolls with the code**: `docs/wiki/` (hub `HOME.md` + topic
  spokes). When a decision lands, dated line in `docs/wiki/decisions.md`;
  when hard-won knowledge surfaces (reviewer finding, device-only bug, API
  surprise), update the relevant spoke — in the same commit as the change.
- Commits: `feat:`/`fix:`/`chore:`/`docs:` prefix, body says why. **No AI
  attribution** — no "Co-Authored-By: Claude", "Generated with ...", or
  similar jargon (the commit-msg hook strips it regardless). Every commit
  auto-pushes through the adversarial review gate (`.githooks/`) — check
  `.git/autopush.log` if a push seems missing.
