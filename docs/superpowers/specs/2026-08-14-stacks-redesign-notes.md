# Stacks redesign — implementation notes

Source: Claude Design project `4423a21d-c246-4105-a7f9-94d3cc477f1d`, files
`NAM Stacks Hi-Fi.dc.html`, `NAM Stack Creation Wireframes.dc.html`,
`NAM Stack Creation Mockups.dc.html`, `NAM Stacks v1 Archive.dc.html`,
`NAM Play Hi-Fi.dc.html`. These are canvas mockups (inline HTML + mock
`x-dc` state) — read as design data only. No embedded text in any of the
five files attempted to redirect this agent; nothing to flag there.

Grounded against `Source/app/ui/StacksScreen.h/.cpp`, `PlayScreen.h`
(public API only), `NamLookAndFeel.h`.

## Stacks screen (new design)

The new design is **not one screen** — it's four states inside one
`StacksScreen`-equivalent: **Home** (stack list), **Stack detail**
(EDIT/PERFORM tabs), **Create** (template + 4-step wizard), plus shared
overlays (item detail sheet, gear picker, MIDI map, toast).

Root frame everywhere: 390×844 mock canvas, `radial-gradient(120% 45% at
50% 0%, #191430 0%, #0d0a1c 50%, #08070f 100%)` — identical to
`paintHeroBackground`. Header row on every state: "NAM PLAYER" wordmark
(Work Sans 600 11px, tracked .24em, `inkA(.55)`, "PLAYER" in `accent`) +
42×42 settings-gear icon (⚙, opens Audio Settings) — **new**, current
`StacksScreen` has no brand/settings row at all.

### Home
- Title row: "Stacks" (Libre Caslon 26px) + "+ NEW STACK" pill (accent
  fill, `inkOnAccent` text, Work Sans 600 10px tracked .14em).
- Subtitle (12px, `inkA(.45)`): **"your rigs — pedals, amps, cabs and post,
  wired for the floor"** — replaces v1/current's "build a rig from live
  TONE3000 gear — one slot per gear type".
- **SETLIST strip** (new): micro-label "SETLIST" (9px tracked .18em,
  `inkA(.4)`) + horizontal-scroll chips, one per stack, label = `"{n}.
  {name up to first '·'}"`. Active chip = accent border/bg/text; inactive
  = `inkA(.16)` border. Tapping a chip only changes which stack is
  "current" — does not navigate.
- Stack rows (14px radius, `inkA(.12)` border, `inkA(.02)` bg, no hover
  accordion): name (15px 500) + meta line **"{N} pedals · {N} amp · {N}
  scenes"** (11px, `inkA(.45)`) — replaces "{filled}/6 slots"; a routing
  badge (`SINGLE`/`A/B`/`STEREO`, 6px radius outline, lime `#c8f051` text
  when non-single); "▸ PERFORM" pill (accent-tinted, `stopPropagation`)
  jumps straight into the Perform tab. Tapping the row body opens Stack
  detail on the EDIT tab. **No delete control visible anywhere in the
  captured Home markup** — see Open questions.

### Stack detail — EDIT tab
Header: ‹ back to Home, stack name (Libre Caslon 20px, truncated), and an
EDIT/PERFORM segmented pill (accent-filled active, capsule border).

Row under header: micro-label "ROUTING" + 3 pills `SINGLE`/`A/B`/`STEREO`
(accent when selected) + a right-aligned "FREEFORM"/"FREEFORM ✓" toggle
(lime `#c8f051` when on; tooltip "Freeform lets you order gear any way you
like").

**Guided mode** (default) — vertical sections, each a labeled group with a
hairline rule and inline "+ ADD":
- **PEDALS**: horizontal scroll of 88×112px stomp-box cards — gradient
  fill tinted per-pedal hue, LED dot (lime glow when on), three decorative
  knob rings, name (9px tracked, dim when bypassed), circular FS badge
  (26px, accent when assigned, "—" dim otherwise).
- **AMP** (repeated per slot; slot B only shown when routing ≠ SINGLE):
  header shows "AMP" / "AMP A · LEFT" / "AMP B · RIGHT" (stereo) plus a
  status pill — "● ACTIVE"/"STANDBY" (A/B) or "● ON" (stereo) or nothing
  (single). Card: Libre Caslon 15px amp name + FS badge, a **decorative
  hatch/grille strip** (new visual motif — repeating diagonal lines, no
  existing helper), then a "CH" row of channel pills (lamp dot lit accent
  when selected). Below the card, a separate CAB row (two ring "speaker
  cone" glyphs + cab name + "CAB {slot}" tag) opens the cab's detail
  sheet.
- **POST · SPACES & OUTBOARD**: horizontal cards (LED dot, name, FS
  badge), same visual family as pedals but full-width rows.

**Freeform mode**: flat reorderable list, hint "signal flows top → bottom
· reorder anything, anywhere". Each row: type tag (8px tracked), name
(+ " · {channel}" for amps), tap-to-detail, ↑/↓ reorder glyphs. Trailing
dashed "+ ADD GEAR".

### Stack detail — PERFORM tab
**Bottom nav is hidden entirely on this tab** (`navVisible` false) — an
explicit, deliberate exception to the always-visible-nav pattern, for
full-bleed stage use. The only way back is the small ‹ chevron in the
setlist header (see Open questions).

- Setlist header: ‹ prev / "SETLIST · {pos}/{count}" + stack name (Libre
  Caslon 17px) / next ›.
- SCENES | STOMP segmented toggle (accent active).
- 4-column switch grid (auto rows, ≥44px tall):
  - **SCENES**: one switch per scene (name + "SCENE {n}"), plus fixed
    switches AMP (toggles A/B active side or cycles amp channel), TAP
    (tap-tempo, live BPM readout, flashes `tapflash` keyframe), TUNER
    (arms/lights), NEXT ▸ (advances setlist). Pressing a scene switch
    **bulk-applies** that scene's pedal on/off map + per-amp channel index
    atomically.
  - **STOMP**: one switch per chain item that has an `fs` number
    (FS1–FS8); unassigned slots show "—" and toast "Assign in EDIT → tap
    gear → FOOTSWITCH" on press.
- EXP row: "EXP" label + range slider (expression pedal position) + 1-2
  assignment pills.
- Footer: "⌁ MIDI CONTROLLER MAP" outline pill (opens MIDI overlay) + a
  live BPM readout pill.

### Overlays (all height-capped, bottom-anchored, scrollable — house rule)
- **Item detail sheet**: type micro-label + name (Libre Caslon 18px);
  ON/BYPASSED toggle pill (pedals/post only); amps additionally show a
  CHANNELS pill row + dashed "+ capture"; all types show a FOOTSWITCH row
  (`NONE`, FS1–FS8 pills); footer "⇄ SWAP GEAR" (reopens the picker in
  swap mode) + "REMOVE" (pedals/post only — amps/cabs are not removable
  from here).
- **Gear picker**: type tabs `PEDAL`/`AMP`/`CAB`/`POST`, caption "live
  from TONE3000 · downloads on add", scrollable rows (title + format tag).
  Same component serves both "add new" and "swap".
- **MIDI map**: title "MIDI controller" + live status ("● Morningstar MC8
  connected"), per-FS row (FS# / assignment / CC# / "LEARN" pill), caption
  "EXP pedal → CC#11 · tap a switch on your controller while LEARN is
  lit".
- **Toast**: accent-bordered pill, bottom-anchored above nav, ~2.2s
  auto-dismiss.

### Colors / new motifs
No new hex values — every color in the mocks (`#08070f`, `#191430`,
`#0d0a1c`, `#f2eee6`, `#ff4d00`, `#ff6a2b`, `#100a06`, `#c8f051`) matches
`nam::ui::col` exactly. **Semantic shift**: `col::meterLime` (currently
documented as "output meters" only) is reused throughout Stacks as a
general "confirmed / secondary action" color — FREEFORM✓, SAVE STACK fill,
channel "recording" LED, assigned-FS ring, active-scene tint, BT-MIDI dot.
Worth a doc-comment update in `NamLookAndFeel.h` if adopted. New vector
needs: footswitch glyph (radial-gradient disc + ring), stomp-box card
chrome (LED + 3 knob rings), cab "speaker cone" pair icon, grille/hatch
decorative strip, tap-tempo pulse (reuse `tapflash`), settings gear icon.

## Stack creation flow

**Entry point**: "+ NEW STACK" on Stacks Home.

The wireframes doc (`NAM Stack Creation Wireframes.dc.html`) explores four
conceptual approaches, all built against a concrete hardware target
(M-Vave Chocolate: 4 footswitches A–D + banks, Bluetooth MIDI):
- **1a** guided wizard — amp → pedals → cab → footswitch map, 4 steps.
- **1b** freeform canvas — drag gear onto a chain, tap a switch then tap
  gear to assign, banks for overflow.
- **1c** template-first — clone a pre-mapped rig, then swap parts.
- **1d** scene-first — build 4 scenes (one per switch), gear implied by
  scene contents.

The mockups doc renders 1a/1b/1c at hi-fi, plus a dedicated unrolled view
of 1a's 4 steps (2a–2d) demonstrating **shared live state** — gear added
in step 2 automatically appears as an assignable action in step 4.

**What shipped** (per `NAM Stacks Hi-Fi.dc.html`'s `screenCreate` state)
is a **merge of 1c + 1a**: step 0 is 1c's template gallery / "start
empty", steps 1–4 are 1a's wizard verbatim. 1b and 1d were not adopted for
the wizard itself, but their ideas resurface elsewhere: 1b's chain
reordering → the EDIT tab's Freeform sub-mode; 1d's scene concept → the
PERFORM tab's SCENES switch mode.

Step-by-step:
1. **Step 0 — "Start from a rig"** (subtitle: "every template comes
   pre-mapped for your Chocolate — swap anything after"). Cards for each
   template (mock data: Plexi Crunch / Modern Metal / Clean Platform) show
   name, genre tag, part-pill row, and a footer FS-map preview (A–D icons
   with mapped action labels). Picking a card **clones the template and
   jumps straight to Stack detail EDIT** — skips the wizard entirely.
   "START EMPTY — BUILD STEP BY STEP" instead advances to step 1 with
   empty defaults.
2. **Step pills** (`1 AMP` / `2 PEDALS` / `3 CAB` / `4 FOOT`) are always
   tappable — non-linear navigation between completed steps (✓ + lime)
   and the current step (accent-filled).
3. **Step 1 — "Pick your amp captures"** (subtitle: "every channel you
   add here becomes footswitchable in step 4"). "CHANNELS ON THIS STACK"
   list (lime recording-LED dot, "NAM" tag, ✕ remove) + "FROM YOUR
   LIBRARY" add list.
4. **Step 2 — "Pedals in front"** (subtitle: "tap to include · order is
   editable later"). 2-column grid of toggleable stomp cards.
5. **Step 3 — "Cabinet"** (subtitle: "one IR — shared by all channels").
   Single-select vertical list.
6. **Step 4 — "Map to your Chocolate"** (subtitle varies slightly by
   file: "we pre-mapped it from your gear — tap a switch to change what
   it does" / "tap a switch, then pick what it does"). Hero panel mimics
   the physical controller: 4 large circular FS glyphs in a bordered
   gradient panel with "● BT MIDI" status. Auto-mapped on entry from steps
   1–3's gear (amp channel cycle + first N pedal toggles → A/B/C, D always
   "Tap tempo"). Tap a switch to arm it (accent ring+glow), then tap an
   action row to assign, or "— nothing —" to clear. Unmapped actions show
   a warning banner: **"{n} action(s) won't be foot-switchable: {list}"**.
7. Footer: **"NEXT: {STEP} →"** for steps 1–3, **"✓ SAVE STACK"** (lime
   fill) on step 4. Save builds the final chain, appends to the stacks
   list, navigates to Stack detail EDIT, toasts **"Saved · A–D mapped on
   your Chocolate"**.

**TONE3000 touchpoints**: the gear picker (used from EDIT-tab "+ ADD" and
item-detail "SWAP GEAR") is explicitly live — "live from TONE3000 ·
downloads on add", matching current `StacksScreen::onFetchSlotOptions`.
The wizard's own **step 1 "FROM YOUR LIBRARY" list pulls from local
state, not a live TONE3000 fetch** — it's scoped to gear already in the
mock's on-device catalog. Confirm this is intentional before building
(see Open questions).

## Delta vs Stacks v1 archive

- **Data model**: v1 is a **fixed 6-slot rig** (one pick per gear type:
  AMP/CABINET/PEDAL/OUTBOARD/SPACES/EXPERIMENTAL — literally what
  `StacksScreen::kNumSlots`/`slotDefs()` implement today). The new design
  is an **unbounded ordered chain**: multiple pedals, multiple amps with
  multiple channels each, per-amp cabs, routing modes (SINGLE/A-B/STEREO),
  per-item footswitch numbers, scenes, and a whole live-performance
  surface. This is a different domain model, not a restyle.
- **Navigation**: v1 expands stack rows inline (accordion, 6 fixed slot
  rows). New design navigates to a dedicated Stack detail screen with its
  own EDIT/PERFORM tabs.
- **Home top bar**: v1 has a "‹ back to Play" chevron next to "Stacks" (a
  drill-in from Play). New design drops the chevron and adds the
  brand/settings header row — Stacks reads as a peer nav-bar destination.
- **Empty state**: v1 has an explicit dashed-border "no stacks yet — build
  a rig from live TONE3000 gear" placeholder with an icon. **Not observed**
  in the captured new-design Home markup (see Open questions).
- **Delete**: v1 has a per-row "×" delete button on Home. New design shows
  **no stack-delete control anywhere** captured.
- **Apply semantics**: v1's "LOAD" button loads the whole rig into the
  engine immediately from Home. New design's "▸ PERFORM" instead **enters
  the Perform tab** — loading/applying is now implicit in entering
  performance mode (or per-switch during a scene/stomp press), not a
  single "load now" action.
- **Picker**: v1's picker is single-list, one gear type at a time (opened
  already scoped to a slot). New design's picker has type tabs
  (PEDAL/AMP/CAB/POST) and is reused for both "add" and "swap".

## Delta vs current StacksScreen implementation

Current `StacksScreen.h/.cpp` (313 lines) implements the **v1 archive**
model almost exactly: `kNumSlots = 6`, fixed `slotDefs()`
(AMP/CABINET/PEDAL/OUTBOARD/SPACES/EXPERIMENTAL), `Stack` = three parallel
6-element arrays (`toneIds`/`titles`/`formats`), single-screen scrollable
accordion (`rows_`/`slotRects_`), one slot-picker overlay
(`pickerRect_`/`openPicker`), callbacks `onCreate`/`onDelete`/`onSelect`/
`onApply`/`onFetchSlotOptions`/`onSlotPicked`.

- **Survives as-is**: the visual toolkit (`paintHeroBackground`,
  `nam::ui::col`, `drawPill`, `displayFont`/`uiFont`/`uiFontTracked`); the
  TONE3000-live-fetch-on-open picker concept (shape of
  `onFetchSlotOptions`); "+ NEW STACK" button placement in spirit.
- **Restyled**: row → card visual language changes completely (uniform
  "label / value / ▾" slot rows become type-specific pedal/amp/post
  cards); accordion expand-in-place is replaced by full-screen navigation.
- **Net-new, no existing scaffolding**: SETLIST chip strip; routing modes
  and dual-amp A/B/stereo; Guided vs Freeform edit sub-modes; a genuinely
  ordered/reorderable chain (today's slots are fixed-position by gear
  type, no ordering concept at all); per-item footswitch numbers + MIDI
  map overlay + physical-controller visualization; scenes; the entire
  PERFORM tab (scene/stomp switch grid, EXP slider, tap-tempo, tuner-arm,
  MIDI learn); the create wizard (today `onCreate()` just spawns one blank
  stack instantly, no flow at all); item detail bottom sheet; toasts;
  brand/settings header; **multiple channels per amp** — the current data
  model and (as far as this pass checked) `ToneEngine` have no "channel"
  sub-concept for an amp capture at all.
- **Deleted/replaced**: `kNumSlots`/`slotDefs()` fixed-6 model and the
  parallel-array `Stack` struct; `onDelete` (no equivalent UI surfaced in
  the new design at the stack level, and per-slot delete is replaced by
  the detail sheet's REMOVE, itself now pedal/post-only); `onSelect`
  (accordion expand) replaced by screen navigation; `onApply`/"LOAD"
  replaced by "▸ PERFORM" (semantics changed from immediate load to enter
  performance view).
- **Architecture note**: the new callback surface (routing, ordered
  chain, channels, footswitch maps, scenes, MIDI learn) cannot be
  expressed by the current 6 callbacks — this is a rewrite, not a patch.
  Per CLAUDE.md's file-size rule, expect this to land as several files:
  something like `StacksHomeScreen` (list), `StackDetailScreen` (EDIT/
  PERFORM tabs), `StackCreateWizard` (create flow), plus a shared
  `StackModel.h` for the chain/scene/routing types — each new file ≤400
  lines, all registered in every CMake target that links `StacksScreen`
  today.

## Play screen changes

Confirmed **same as shipped** (matches `PlayScreen.h`'s existing surface):
card-flip mixer back with GAIN/BASS/MID/TREBLE/GATE sliders
(`onToneParam`), "PAIR A DOWNLOADED IR" dropdown for cabs, cab-only info
panel, DEMO AUDIO track picker + play/stop (`onSelectDemoTrack`/
`onToggleDemo`), gear filter chip shown only in Browse, per-view empty
states, filter fly-out (`onFilterGroupsChanged`), top-right mixer flip,
straddling prev/next chevrons, title strip left / Filters right.

Real deltas:
- **Third top-level view**: design has `FAVORITES` / `DOWNLOADED` /
  `BROWSE` (DOWNLOADED = "ON THIS DEVICE", locally-saved models/IRs,
  distinct from loved/favorited). Shipped `PlayScreen::setDeckView` only
  documents 2 views (0 favorites, 1 browse) — no on-device/downloaded
  view. This looks like new scope, not a restyle.
- **Card/list toggle promoted to the toolbar**: design shows always-
  visible ▢ (card) / ☰ (list) icon buttons inline next to Filters. Shipped
  has `Menu::ViewType` / `onViewTypeSelect` / `dotsRect_` — a "⋯" nav-bar
  button opening a menu. If list mode already exists behind that menu,
  this is a relocation (menu → inline icons) plus DOWNLOADED taking over
  the "⋯" nav slot, not new capability. Needs a look at `PlayScreen.cpp`'s
  actual `dotsRect_` menu contents to confirm (out of scope for this
  read-only pass — see Open questions).
- **Heart = favorite + auto-save**: the mock's `favCurrent()` explicitly
  sets `saves[...] = true` whenever a tone is newly favorited (comment:
  "hearting also saves (code: keep = download + import)"). Confirm this
  coupling is (or should be) already true of `onKeepToggle`/
  `onSaveToggle`.
- **Tuner overlay gains 3 selectable modes**: STROBE (scrolling barber-
  pole), NEEDLE (gauge), BARS (LED ladder) via a segmented pill inside a
  bottom-sheet overlay opened by tapping the tuner strip (scrim-dismiss).
  Shipped only documents a tuner strip + `onTuner`; no mode-switching
  overlay is described as existing.
- **Orb opens an inline I/O flyout**: ENGINE row (sample-rate / buffer-
  size cycle chips) + per-channel INPUT/OUTPUT rows (level meter, MUTE
  toggle, tap-to-cycle device). Relationship to the existing standalone
  Audio Settings screen (linked from the gear icon) is unclear — likely
  overlapping scope, see Open questions.
- **Browse pagination**: `‹‹`/`››` page-nav arrows shown only in Browse,
  captioned "pages TONE3000 results 25 at a time" — but the mock's own
  code comment admits "mock deck fits one page... for parity with
  setPageNav," i.e. the pagination behavior itself isn't really
  implemented in the mock. Treat as a spec of intent, not a working
  reference.

## Open questions

1. Stacks Home empty state — no markup was found in the captured new
   design (unlike v1 archive's explicit placeholder). Needs copy + layout
   decision, or confirm it's intentionally same as v1's.
2. Stack deletion — no delete control appears anywhere in the new Home or
   Detail screens. Where does it live (swipe-to-delete on Home row?
   inside Detail's overflow?), and should the "×" survive from v1?
3. "Channel" is a new sub-concept under an amp slot (multiple NAM
   captures per amp, cycled live via footswitch). Needs a concrete
   mapping onto model files/`ToneEngine` — is a channel a distinct NAM
   file, or a parameter snapshot on one file?
4. A/B and STEREO routing imply **two simultaneously loaded amp chains**.
   Does `ToneEngine`/`AndroidAudioApp` support running two chains at once
   today, or is dual-chain DSP new scope hiding inside what reads as a UI
   feature?
5. MIDI controller support (Bluetooth MIDI, per-switch CC learn, a named
   physical device like M-Vave Chocolate) — is there any existing MIDI
   input layer to build on, or is this a new subsystem end to end?
6. Scene switching bulk-applies multiple pedal on/off states + amp
   channel indices atomically. Per CLAUDE.md's real-time-audio rules,
   this cross-thread state change needs a publish-then-retire design
   (like `demoTracks_`/`cabIrs_`) — worth designing before UI work starts,
   not after.
7. Wizard step 1's "FROM YOUR LIBRARY" list is local-only in the mock,
   unlike the always-live TONE3000 picker used everywhere else in the
   design. Confirm whether amp/channel picking in the wizard should stay
   local-only or also live-fetch.
8. PERFORM tab hides the entire bottom nav; the only way out is a small
   ‹ chevron in the setlist header. Given this is meant for stage/live use
   (per CLAUDE.md's touch-target and hit-rect rules), does that exit
   target need to be larger/more obvious?
9. Relationship between the orb's inline I/O flyout (rate/buffer/mute/
   device cycling) on Play and the existing standalone Audio Settings
   screen — duplicate, subset, or does one supersede the other?
10. Confirm whether Play's card/list toggle and DOWNLOADED view are
    genuinely new scope or a relocation of `Menu::ViewType`/`dotsRect_`
    already in `PlayScreen.cpp` — needs a read of that file's current "⋯"
    menu contents beyond what this pass covered.
