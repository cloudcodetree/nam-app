# Stacks

Stacks is a list of rigs. Each rig is an **ordered chain** of gear
(`nam::StackModel`, `Source/model/StackModel.h` -- JUCE-free,
headless-tested) that you edit directly. A `Stack` is `{uid, name,
chain[]}`; a `ChainItem` is `{uid, type (Pedal/Amp/Cab/Post), toneId, title,
format, gearTag, imageUrl, bypassed, channels[], activeChannel}`. Amps hold
multiple switchable channels -- the item sheet switches the active one, and
that's audible.

Stripped 2026-08-15 (Chris): guided mode, the create wizard, CONTROLS/
PERFORM (a stage view), scenes, routing (SINGLE/A-B/STEREO), and footswitch
assignment are all gone. See decisions.md for why.

- **Persistence**: `stacks.json` in appdata via `loadStacksJson`/
  `saveStacksJson`, shape `{"version":2,"stacks":[…]}`. `StackModel::parse`
  migrates the v1 shape (`[{name, slots:[{id,title,format}×6]}]`, slot order
  AMP/CABINET/PEDAL/OUTBOARD/SPACES/EXPERIMENTAL) transparently; the next
  save writes v2. Parse never throws and isolates per stack/item/slot, so
  one bad object can't destroy the library; a file whose top-level shape
  isn't recognized (`looksLikeStacksFile`) is copied to `stacks.json.bak`
  before the first overwrite. Stack uids are minted on parse when absent.
  Retired v2 keys ("routing", "scenes", "activeScene" on a stack; "fs" on
  an item) are no longer model fields -- `kStackKeys`/`kItemKeys` no longer
  list them, so parse's per-key `extra.erase(k)` never touches them and
  they round-trip untouched in `Stack::extra`/`ChainItem::extra` instead of
  being dropped. A file written by a build that still had PERFORM/scenes
  keeps that data on disk even though nothing in the app reads it anymore.
- **Engine truth: ONE model + ONE IR.** The active amp channel's tone is
  the model; the cab item is the impulse. Pedal/post items are stored +
  visual only (bypass toggles in the item sheet, no audible effect) --
  there's no multi-node DSP chain yet. This is the ceiling the surface was
  stripped back to match.
- **Surfaces**: `StacksHomeScreen` (the rig list; "+ NEW STACK" mints an
  empty rig and opens its editor directly) and `StackDetailScreen`, whose
  body is always `StackEditView` -- freeform is the only edit mode there
  is: signal top-to-bottom, tap a row for the item sheet, ↑/↓ to reorder,
  "+ ADD GEAR", REMOVE STACK with an inline confirm. The gear picker
  (`StackGearPicker`) and item sheet (`StackItemSheet`, minus its old
  FOOTSWITCH row) are overlays owned by `StackDetailScreen` so they paint
  over its header too.
- **Editing is audible.** Opening a rig and every edit that changes the
  audible truth (swap gear, cycle an amp channel, reorder so a different
  amp/cab becomes active, remove the live amp/cab) applies to the engine at
  once. Apply wiring lives in `Source/app/ui/AppShellStackApply.cpp`
  (renamed from `AppShellPerform.cpp` in the same pass): `applyStackToEngine`
  fires from state transitions -- `AppShell::openStackDetail` (right after
  `show()`, since the apply is guarded on the screen being `current_`,
  which only `show()` makes true) and each mutation callback in
  `AppShellStacks.cpp` (`onChanged`, `onRemove`, `applyGearPick`,
  `mutateItem`) -- **never** from `pushStacks()`, which is a plain render
  call `finishToneLoad` also invokes on its failure path; hooking the apply
  there once turned an unloadable tone into an unbounded retry (gate
  BLOCKER, fixed before PERFORM even existed as a name -- see the
  2026-08-15 entries below).
  It is idempotent (each half -- model, IR -- is skipped when that tone is
  already live) and guarded on visibility (`current_ ==
  stacksDetail_.get()`), which survives navigating to Play (where `show()`
  clears `liveModelToneId_`/`liveIrToneId_`) without a late-completing load
  overwriting a tone the user picked there instead.
  One `nam::ToneInfo` load is in flight at a time
  (`performApplyInFlight_`), with **two** pending slots --
  `pendingModelApply_` and `pendingIrApply_`, keyed by `tone.format`, both
  drained on completion (last request wins *within* each resource; a
  single slot let an IR request clobber a parked model request). A 30s
  `ApplyTimeout` watchdog resolves the in-flight state as a failure if the
  host callback never fires.
- **Local-first apply**: a chain item built by the now-retired create
  wizard references the local library by `LibraryEntry::id` (a filename,
  `keep_<id>.nam`/`ir_<id>.nam`), not a TONE3000 tone id -- such items can
  still exist in an on-disk `stacks.json` even though nothing creates new
  ones anymore. `startToneLoad`'s `findLocalEntry` matches the id against
  `getModels_`/`getIrs_` by format and applies synchronously via
  `loadModel_`/`loadIr_` through the same completion/drain machinery; no
  match falls through to `svc_.loadTone` (the network route the EDIT
  picker's items -- real TONE3000 ids -- always use). The two id spaces
  can't collide.
- Bypass and an amp's `activeChannel` are STORED-state writes (visual only
  -- no multi-pedal DSP chain exists in Phase A) and apply immediately,
  independent of whether the audible load below them succeeds.
- Deferred to their own future plans: MIDI/foot control, dual-amp A/B +
  stereo DSP, audible pedal nodes, guided/template rig building. None of
  that scaffolding exists in the UI anymore -- it gets rebuilt, if at all,
  once the engine can actually do it.
