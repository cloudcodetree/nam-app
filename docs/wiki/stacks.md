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

## Create wizard (Task 5, final Phase A piece)

`Source/app/ui/StackCreateWizard.h/.cpp` (+ `...Gear.cpp` for gear
mutation/FS-assignment logic, `...Paint.cpp` for painting, and the
data-only `StackTemplates.h`). Replaces the old `onCreate` behavior of
spawning one blank stack instantly.

- **Hosting**: a direct (non-pointer) member of `StacksHomeScreen`, sized
  to its full local bounds and shown/hidden via `open()`/`close()` — same
  pattern as `StackDetailScreen`'s `picker_`/`itemSheet_` members. This
  means `AppShell.cpp`/`AppShellChrome.cpp` needed no new Screen
  enumerator, no `show()`/`resized()` changes, and no `contentBounds()`
  changes; the owner (`AppShellStacks.cpp`'s `wireCreateWizard`) just
  wires callbacks onto `stacksHome_->wizard()`. The only touch to
  `AppShell.cpp` is an 8-line `handleBackButton` hook
  (`stacksHome_->closeWizard()`), ordered before the Detail-overlay
  chain since the wizard also lives under `Screen::Stacks`.
- **Step 0 (gallery)**: 3 built-in templates (`nam::templates::builtins()`
  in `StackTemplates.h`) — `nam::Stack` literals with empty `toneId`s but
  real placeholder titles/channels/FS numbers. Picking one clones it with
  a fresh `nam::StackModel::nextUid` sequence and calls `onSave(stack,
  false)` — no toast, jumps straight to Detail EDIT (matches the spec:
  template picks are silent). "START EMPTY" advances to step 1 against an
  empty `draft_` instead.
- **Steps 1-3** mutate a local `draft_` (steps only touch it, nothing
  persists until SAVE): step 1 pushes/pops `StackChannel`s on the
  stack's single Amp `ChainItem` (created lazily on first pick, same
  "channels[0] mirrors toneId/title" convention as `AppShell::
  applyGearPick`'s AddChannel path); step 2 toggles Pedal `ChainItem`s by
  `toneId` match; step 3 singleton-replaces the Cab item. **Gap**: `nam::
  LibraryEntry` (`Source/model/LibraryEntry.h`) has no gear-type tag —
  only `LibraryType::Model` vs `Ir` — so steps 1 and 2 both list the same
  full `onFetchModels()` result; there's no data-level way to show "amps
  only" vs "pedals only" today. A real gear tag on `LibraryEntry` is the
  fix, not attempted here.
- **Step 4**: an action list built fresh from `draft_.chain` each call
  (`buildActions()`) — an amp-channel-cycle action (only if the amp has
  >1 channel), one on/off action per pedal, and a fixed Tap-tempo action
  with no backing `ChainItem`. `switches_[4]` (A-D) is the single source
  of truth; auto-map runs once per wizard session on first entry to step
  4 (amp cycle → A, pedals → B/C in order, D always Tap tempo), and
  re-entry after editing gear elsewhere prunes stale bindings instead of
  re-running auto-map (so a manual re-assignment survives non-linear step
  navigation). `syncFsIntoChain()` writes switch index+1 onto the bound
  `ChainItem::fs` — the SAME field PERFORM's STOMP grid already reads, so
  a wizard-assigned switch is live in PERFORM immediately; a "channel
  cycle" assignment currently reads as a plain bypass toggle there (no
  MIDI/cycle semantics exist yet — tracked, not fixed).
- **Save**: `onSave(nam::Stack, bool toast)` — the `bool` is a deliberate
  deviation from the task brief's literal `onSave(nam::Stack)` signature,
  needed so the owner can skip the "Saved · A-D mapped on your Chocolate"
  toast on the (silent) template-pick path while still firing it for the
  step-4 guided save.
