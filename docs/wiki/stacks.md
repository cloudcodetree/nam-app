# Stacks

User-built rigs as an **ordered chain** of gear (`nam::StackModel`,
`Source/model/StackModel.h` — JUCE-free, headless-tested). A `Stack` is
`{uid, name, routing, chain[], scenes[], activeScene}`; a `ChainItem` is
`{uid, type (Pedal/Amp/Cab/Post), toneId, title, format, gearTag, fs,
bypassed, channels[], activeChannel}`. Amps hold multiple switchable
channels; scenes snapshot a pedal-bypass map plus an amp channel.

- **Persistence**: `stacks.json` in appdata via `loadStacksJson`/
  `saveStacksJson`, shape `{"version":2,"stacks":[…]}`. `StackModel::parse`
  migrates the v1 shape (`[{name, slots:[{id,title,format}×6]}]`, slot order
  AMP/CABINET/PEDAL/OUTBOARD/SPACES/EXPERIMENTAL) transparently; the next
  save writes v2. Parse never throws and isolates per stack/item/slot, so
  one bad object can't destroy the library; a file whose top-level shape
  isn't recognized (`looksLikeStacksFile`) is copied to `stacks.json.bak`
  before the first overwrite. Stack uids are minted on parse when absent.
- **Engine truth is unchanged: ONE model + ONE IR.** The active amp
  channel's tone is the model; the cab item is the impulse. Pedals/post
  items and the routing enum (SINGLE live; A/B and STEREO stored only) are
  stored + visual in Phase A — no multi-node DSP chain exists yet.
- **Surfaces**: `StacksHomeScreen` (setlist chips, per-stack meta + PERFORM
  pill), `StackDetailScreen` hosting `StackEditView` (guided sections +
  freeform reorder, gear picker, item sheet, REMOVE STACK) and
  `StackPerformView` (below), and `StackCreateWizard` (below).
- Deferred to their own plans: MIDI/foot control, dual-amp A/B + stereo
  DSP, audible pedal nodes.

## PERFORM tab

- `StackDetailScreen`'s PERFORM tab hosts `StackPerformView`
  (`Source/app/ui/StackPerformView.h/.cpp` + `...Paint.cpp`), full-bleed
  *within Detail's own area* — it's given Detail's whole bounds, not just
  the body, so none of Detail's own brand header/back-chevron/tab pill
  paints or hit-tests while it's showing. The global bottom nav is
  unaffected and stays visible/tappable, same as every other screen (see
  decisions.md, 2026-08-15) — Detail itself still lays out inside
  `AppShell::contentBounds()`.
- Apply wiring lives in `Source/app/ui/AppShellPerform.cpp`
  (`AppShell::wirePerformView`/`enterPerform`/`applyScene`/`applyAmpCycle`/
  `stepPerformStack`/`requestToneLoad`/`startToneLoad`), split out of
  `AppShellStacks.cpp` to stay under the 400-line new-file cap.
- A scene/AMP tap is audible only when the target toneId differs from
  `AppShell::liveModelToneId_`/`liveIrToneId_`. Those are updated by
  PERFORM's own applies and **cleared whenever Play becomes the nav target**
  (`AppShell::show`), since a Play/Browse-side load would otherwise leave a
  stale id that a later PERFORM re-entry trusts — worst case now is one
  redundant reload. One load in flight at a time
  (`performApplyInFlight_`), with **two** pending slots —
  `pendingModelApply_` and `pendingIrApply_`, keyed by `tone.format`, both
  drained on completion (last tap wins *within* each resource; a single
  slot let an IR request clobber a parked model request). A 30s
  `ApplyTimeout` watchdog resolves the in-flight state as a failure if the
  host callback never fires, so PERFORM can't wedge.
- **Local-first apply**: wizard-built items reference the local library by
  `LibraryEntry::id` (a filename, `keep_<id>.nam`/`ir_<id>.nam`), not a
  TONE3000 tone id. `startToneLoad`'s `findLocalEntry` matches the id
  against `getModels_`/`getIrs_` by format and applies synchronously via
  `loadModel_`/`loadIr_` through the same completion/drain machinery; no
  match falls through to `svc_.loadTone` (the network route EDIT's picker
  stores real ids for). The two id spaces can't collide.
- Bypass + `activeScene` + the amp's `activeChannel` are STORED-state
  writes (visual LEDs/highlight only — no multi-pedal DSP chain exists in
  Phase A) and apply immediately, independent of the audible load; on a
  failed scene load, `activeScene` and the amp's `activeChannel` are
  reverted together (bypass is not, since it never depended on the load).
- The bottom nav is persistent on PERFORM (as on every screen) by Chris's
  direction; the `setNavHidden` mechanism that used to hide it here was
  deleted, not defaulted off -- see decisions.md, 2026-08-15.

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
- **Save**: `onSave(nam::Stack, juce::String toast)` — the wizard composes
  the toast text so the owner stays dumb: empty on the (silent)
  template-pick path, the spec's "Saved · A–D mapped on your Chocolate"
  when every action is mapped, and a truthful "Saved · {n} action(s) not
  foot-switchable" when the map is incomplete (the copy must not claim a
  full map it doesn't have). Both save paths run `seedScenesFor()` — one
  Scene per amp channel (name = channel title, `ampChannel` = its index,
  `pedalBypass` mirroring the as-built state, `activeScene = 0`) so
  PERFORM's SCENES grid is never empty from creation. Interim until a real
  scene editor exists.
