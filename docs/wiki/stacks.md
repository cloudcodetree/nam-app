# Stacks

User-built rigs, one slot per TONE3000 gear type: AMP, CABINET, PEDAL,
OUTBOARD, SPACES, EXPERIMENTAL (`StacksScreen::slotDefs()`).

- **Slots pick from live TONE3000 lists** (trending, per gear; CABINET uses
  `format=ir&gears=cab`) — nothing is downloaded up front. The picked tone's
  `format` is stored per slot and decides how it loads later.
- **LOAD applies the stack** through `AppShell::applyStack`: the first
  filled head-ish slot (AMP → PEDAL → OUTBOARD → EXPERIMENTAL) becomes the
  engine model; the first filled cab-ish slot (CABINET → SPACES) becomes the
  impulse. Engine chain = ONE model + ONE impulse, so that's all that loads.
- Loads are **sequential** (model completes, then cab): the host funnels
  downloads through one session thread and a concurrent second fetch cancels
  the first.
- `doLoadToneLive` (host): cache hit → hot-swap immediately; miss →
  `doDownloadOnly` then swap. `format=="ir"` → `loadImpulseResponse` +
  `setImpulse`; else `NamModel::load` + `setModel`.
- Persistence: `stacks.json` in appdata via `loadStacksJson`/`saveStacksJson`
  services, juce::JSON, shape `[{name, slots:[{id,title,format}×6]}]`.

Known gap: `applyStack` drops load errors silently (no toast) — tracked.

## PERFORM tab (Task 4, v2 ordered-chain model)

The paragraphs above describe the **superseded v1 fixed-slot model**
(`StacksScreen`/`applyStack`); see `StackModel.h` and `docs/wiki/decisions.md`
for the current v2 ordered-chain model. This section covers only PERFORM's
apply wiring, which is new:

- `StackDetailScreen`'s PERFORM tab hosts `StackPerformView`
  (`Source/app/ui/StackPerformView.h/.cpp` + `...Paint.cpp`), a full-bleed
  stage view — it's given the whole screen, not just the body, and none of
  Detail's own brand header/back-chevron/tab pill paints or hit-tests while
  it's showing.
- Apply wiring lives in `Source/app/ui/AppShellPerform.cpp`
  (`AppShell::wirePerformView`/`enterPerform`/`applyScene`/`applyAmpCycle`/
  `stepPerformStack`/`requestToneLoad`/`startToneLoad`), split out of
  `AppShellStacks.cpp` to stay under the 400-line new-file cap.
- Engine truth: ONE model + ONE IR (still, same as v1). A scene/AMP tap is
  audible only when the target toneId differs from `AppShell::
  liveModelToneId_`/`liveIrToneId_` — tracked only by PERFORM's own applies,
  so a tone loaded via Play/Browse first won't be recognized as live until
  PERFORM applies it itself (one redundant reload, not a correctness bug).
  One `nam::ToneInfo` load in flight at a time (`performApplyInFlight_` +
  `pendingPerformApply_`, a single pending slot — last tap wins).
- Bypass + `activeScene` + the amp's `activeChannel` are STORED-state
  writes (visual LEDs/highlight only — no multi-pedal DSP chain exists in
  Phase A) and apply immediately, independent of the audible load; on a
  failed scene load, `activeScene` and the amp's `activeChannel` are
  reverted together (bypass is not, since it never depended on the load).
- `AppShell::setNavHidden` (`AppShellChrome.cpp`) hides the bottom nav for
  PERFORM's full-bleed stage view; see decisions.md for how it's kept in
  sync across every exit path.
