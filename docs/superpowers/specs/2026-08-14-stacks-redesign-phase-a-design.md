# Stacks redesign, Phase A (UI + model) — design

**Date:** 2026-08-14 · **Status:** pending Chris's review
**Source design:** Claude Design project `4423a21d…` (NAM Stacks Hi-Fi,
Stack Creation Wireframes/Mockups); full digest in
`2026-08-14-stacks-redesign-notes.md`.
**Scope decision (Chris, 2026-08-14):** UI + data model against today's
single-amp engine. PERFORM tab ships with ON-SCREEN switches (no MIDI).
Dual-amp A/B/STEREO DSP and the MIDI/foot-controller subsystem are their
own future plans. Stack deletion lives in Detail → EDIT as a REMOVE STACK
row (controller's call, delegated by Chris).

## Goal

Replace the fixed 6-slot Stacks rig with the new design's ordered-chain
model and its four surfaces (Home, Detail EDIT, Detail PERFORM, Create
wizard), audibly driven by the existing single-chain engine.

## Engine truth (unchanged, governs all apply rules)

One model + one IR (decisions.md 2026-08-10). Phase A maps the richer
model onto it honestly:

- The stack's **active amp channel** is the loaded NAM model; the stack's
  **cab** is the loaded IR. Applying either goes through the existing
  `doLoadToneLive` hot-swap path (cache-miss downloads then swaps).
- **Pedals/post items are stored, shown, and toggleable, but not audible**
  in Phase A (no second DSP node exists). Their LEDs/bypass states persist
  and will become audible in the dual-chain plan. The EDIT tab shows a
  one-line hint on pedal/post sections: "visual for now — audio support
  coming" (exact copy in plan).
- **Routing pills render but only SINGLE is selectable**; A/B and STEREO
  show a "coming soon" toast (stored enum so the file format is ready).
- **Scene switch (PERFORM, on-screen tap)** applies: amp-channel change
  (audible, via the same hot-swap) + pedal bypass map (visual). One
  message-thread apply; no new RT-thread work.

## Data model — `nam::StackModel` (JUCE-free, Source/model, TDD)

New header + cpp, headless-tested:

- `GearType { Pedal, Amp, Cab, Post }` (collapses TONE3000's six gears:
  OUTBOARD/SPACES/EXPERIMENTAL → Post for chain purposes; original gear
  string preserved on the item).
- `Channel { toneId, title }` — amps hold 1..n channels,
  `activeChannel` index.
- `ChainItem { id (stable uid), type, toneId/title/format, gearTag,
  fs (0=none, 1..8), bypassed, channels[], activeChannel }` — cab items
  attach to the amp they follow (Phase A: exactly one amp, one cab).
- `Stack { name, routing (Single/AB/Stereo — stored, Single-only active),
  chain (ordered vector), scenes[], activeScene }`. Phase A invariant,
  enforced in StackModel (`canAdd(type)`) and reflected by the picker
  (AMP/CAB tabs disabled with hint "one amp per stack for now" once
  present): at most one amp item and one cab item per stack.
- `Scene { name, pedalBypass map<itemId,bool>, ampChannel }`.
- Serialization: `stacks.json` **v2** (`"version": 2`), with a one-way
  migration from the current v1 parallel-array format: AMP→amp item
  (single channel), CABINET→cab item, PEDAL→pedal, OUTBOARD/SPACES/
  EXPERIMENTAL→post; row order = slot order. Migration is TDD'd against a
  real v1 fixture. Unknown future fields round-trip untouched.
- Pure policy helpers the UI consults: `canReorder`, `activeModelToneId()`,
  `activeIrToneId()`, `sceneApplyPlan(sceneIdx)` (returns what changes).

## Screens (each its own TU ≤400 lines, registered in CMake)

Current `StacksScreen.*` is **deleted** and replaced by:

- **`StacksHomeScreen`** — brand header ("NAM PLAYER" wordmark + ⚙ gear →
  Audio Settings), "Stacks" title + "+ NEW STACK" pill, subtitle
  "your rigs — pedals, amps, cabs and post, wired for the floor",
  SETLIST chip strip (horizontal scroll, active chip accent; tap =
  set-current only), stack rows (name, "{N} pedals · {N} amp · {N}
  scenes" meta, routing badge, "▸ PERFORM" pill → Detail PERFORM;
  row body tap → Detail EDIT). Empty state: v1's dashed placeholder
  restyled with the new subtitle copy.
- **`StackDetailScreen`** — ‹ back, name, EDIT/PERFORM segmented pill.
  - EDIT (guided): ROUTING pill row (SINGLE live, others toast) +
    FREEFORM toggle; PEDALS horizontal stomp-card strip (LED, knob rings,
    FS badge); AMP card (name, grille strip motif, CH channel pills,
    FS badge) + CAB row (cone glyphs, name); POST cards; inline "+ ADD"
    per section → gear picker. Bottom: REMOVE STACK row (confirm sheet).
  - EDIT (freeform): flat ordered list, type tags, ↑/↓ reorder, "+ ADD
    GEAR"; hint "signal flows top → bottom · reorder anything, anywhere".
  - PERFORM (on-screen): hides bottom nav (deliberate stage exception;
    exit chevron hit-rect ≥44px, larger than mock); setlist header
    (‹ prev / "SETLIST · n/N" + name / next ›); SCENES|STOMP toggle;
    4-col switch grid — scenes bulk-apply per Engine truth; STOMP
    switches toggle stored bypass (visual) and flash; fixed switches:
    AMP (cycles channel — audible), TAP (visual BPM only in Phase A),
    TUNER (opens tuner), NEXT ▸. EXP row and "⌁ MIDI CONTROLLER MAP" are
    OMITTED in Phase A (MIDI plan). No nav = back chevron only.
- **`StackCreateWizard`** — step 0 template gallery ("Start from a rig",
  3 built-in templates + "START EMPTY — BUILD STEP BY STEP"; template
  pick clones and jumps to Detail EDIT). Steps 1-4 per mock: 1 AMP
  (channels list + add from library), 2 PEDALS (toggle grid), 3 CAB
  (single-select), 4 FOOT (Chocolate panel visual, tap-switch-then-action
  assignment, auto-map, warning banner) — FS numbers stored for the MIDI
  plan. Footer "NEXT: {STEP} →" / "✓ SAVE STACK" (lime); save toasts
  "Saved · A–D mapped on your Chocolate". Step pills tappable non-linear.
  Wizard step-1 library list = LOCAL library (per mock); the tabbed live
  gear picker remains the add/swap path in EDIT.
- **`StackGearPicker`** (shared overlay TU) — type tabs PEDAL/AMP/CAB/
  POST, "live from TONE3000 · downloads on add", reuses the existing
  live-fetch service shape; serves add + swap.
- **`StackItemSheet`** (shared overlay TU) — ON/BYPASSED (pedal/post),
  CHANNELS row + "+ capture" (amp), FOOTSWITCH row (NONE, FS1-8),
  "⇄ SWAP GEAR", REMOVE (pedal/post only).
- Shared small pieces (toast, FS badge, stomp-card chrome, grille strip,
  cone glyphs) live in a `StackWidgets` TU or NamLookAndFeel additions —
  NOT inlined into screens (file-size rule).

All overlays: height-capped, scrollable, painted last (house rules).
Colors: existing `nam::ui::col` only (digest confirmed zero new hex);
`meterLime`'s doc-comment widens to "confirm/secondary accent".

## AppShell integration

- Nav STACKS → `StacksHomeScreen`; gating unchanged (`kGatesEnabled` +
  `kSoftPaywall` semantics apply exactly as today — Home is the gated
  surface in the internal config).
- New service surface (replaces the six current stack callbacks):
  load/save stacks JSON (same appdata file), live gear fetch (typed),
  `applyModel(toneId)` / `applyIr(toneId)` (existing doLoadToneLive
  split), nav-visibility control for PERFORM (AppShell hides/shows its
  nav bar; back button exits PERFORM first — extends the existing
  paywall-first back-button chain).
- AppShell additions stay wiring-only (over-cap rule); anything bigger
  extracts.

## Error handling

- Apply failures (offline, dead tone id): toast "couldn't load {name} —
  check connection", state unchanged (fixes v1's silent-ignore gap).
- Migration failure (corrupt v1 json): keep file untouched, start empty,
  toast once.
- Scene apply while a previous hot-swap is in flight: queue-drop (last
  tap wins), never stacked loads.

## Testing

- `StackModel` (types, migration, scene plans, serialization round-trip):
  headless TDD, both CMake test targets.
- Screens: emulator E2E per house rule (screenshots in commits): create
  via template, create via wizard, reorder in freeform, swap gear, scene
  switch audibly changes channel, REMOVE STACK, PERFORM nav-hide + back.
- Existing v1 stacks.json on the emulator migrates in place.

## Out of scope (own future plans)

MIDI/foot control (PERFORM's EXP row, MIDI map overlay, CC learn, real
tap-tempo), dual-amp A/B/STEREO DSP + amp-channel preloading, audible
pedal/post nodes, Play-screen deltas from the revised mock (DOWNLOADED
third view, inline card/list toggle, tuner modes, pagination arrows —
the last conflicts with the 2026-08-11 infinite-append decision and needs
Chris's call), template content beyond the 3 built-ins.
