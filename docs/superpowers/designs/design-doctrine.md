# Design doctrine — NAM Player

Distilled from a 2026-08-22 study of HeadRush Prime/Core/Flex, Neural DSP Quad
Cortex and Nano Cortex, Line 6 Helix Stadium, Darkglass Anagram and Dimehead:
60 manuals and firmware changelogs (437k words) plus owner complaints.

**Read this before designing any screen a player touches with a guitar in their
hands.** Every rule below is testable — you can look at a screen and say whether
it complies. Reference screenshots of how competitors solved these problems are
in `refs/`.

---

## What this app is

**An amp that happens to have a screen.** Not a tone browser. The player is
standing up, one-handed, in a dark room, with a guitar occupying both hands most
of the time.

**The one interaction that is ours: the swipe-card deck.** Tones are tracks with
artwork; you swipe through them and heart the ones you keep. Every competitor
browses tones as a *list* — none of them swipes. Never design it away.
(`PlayScreen::layoutMode_ == 0`.)

---

## The cost model — the rule everything else hangs off

Every audible change is either **INSTANT** or **LOADING**, and a screen must
never blur the two.

| Class | What it is | Examples |
|---|---|---|
| **INSTANT** | A parameter or bypass inside the already-loaded model. Allocation-free, cannot fail. | gate, EQ, delay, reverb, in/out gain, chain bypass, mute, solo boost |
| **LOADING** | Swaps a `.nam` model. Costs a real audio gap. | changing rig, changing amp |

Neural DSP states the cause plainly: changing presets causes *"an expected split
second of audio dropout, as switching between Presets re-initializes the whole
signal chain."*

**Design consequence:** a LOADING action needs a visible load state. An INSTANT
action must never show one. If a control's class isn't legible from the screen,
the design has failed.

---

## Scenes

**A scene is a named sparse set of overrides on one rig's engine parameters.**

- A scene **may not change the model.** Every vendor forbids this independently —
  Line 6: *"all snapshots in a particular preset share the same models."*
- A scene **may change the IR**, as a scoped opt-in. Helix Stadium 1.3 added
  "Cab IRs Per Snapshot", **off by default**, toggled per Cab block. The reason
  is cost: an IR is a convolution-buffer swap, a model is a neural-net load.
- Therefore every scene recall is instant and **cannot fail** — no load state, no
  crossfade, no failure path to draw.

**Scoping must be visible.** Quad Cortex's top day-one trap is that parameters
are preset-global until a *hidden two-second long-press* scopes them to a scene —
and downloaded presets ship with no scene assignments, so every new user hits it.
Our answer: tweaking a control while a scene is live scopes it automatically, and
a filled diamond on that row says so.

**There must be a scene LIST.** HeadRush's documented failure is that a scene
exists only as a footswitch *mode* — no list, no count, no way to see them all.

---

## Footswitches

Our budget is the scarcest in the study: **4 switches** (Chocolate Plus), and two
capabilities everyone else relies on are unavailable to us:

- **No hold.** In HID keyboard mode the pedal sends no key-up, so hold is
  physically undetectable.
- **No chords.** A two-switch press may be consumed by the pedal's own firmware
  as a bank change and never reach the app.

Below ~6 switches every vendor builds a **mode system** — and every mode system is
entered by a hold or a chord. We can build neither.

**So all four switches go to scenes.** Four instant, gapless, cannot-fail
actions. Rigs change by thumb between songs, where a gap is free.

**Nothing may hide behind a hold, a chord, or a double-tap.** Not a preference —
the hardware cannot express them.

---

## Reading a screen on a dark stage

- **Position first, brightness second, hue last — and never hue alone.** Which
  item is live must be readable in greyscale, from *which* tile is lit.
- **Never turn an inactive element fully off.** Quad Cortex does this with scene
  LEDs and players report missing switches in the dark. Dim, never absent.
- **Rig name is the only thing that must read at five feet.** But do not mandate
  a size the content can't fill: there is currently **no rename UI**, so names are
  `Rig 3` or a 34-character TONE3000 title. Rename is a precondition.
- **Real units, always visible.** dB, ms, Hz on the target parameter — never
  0–127, never bare percent. Dimehead's own tutorial computes "80% of 128 ≈ 102"
  on camera because there's no numeric entry.

---

## Chain editing

**Reordering is optional.** Nano Cortex's chain is fixed and cannot be reordered
anywhere, on device or in the app. Dimehead — the closest product to ours — has
no chain editing at all: *"select what goes in each fixed slot and turn its
knobs."*

**Never make drag the reorder gesture on a scrolling surface.** This is the
worst defect in the corpus. On the Anagram, drag-to-reorder and swipe-to-scroll
are the same gesture and the drag wins: *"successful scrolling is more of a
lottery."* Explicit ▲▼ buttons or an armed long-press with haptics.

**Don't let the chain run off the edge.** BIAS FX 2 Mobile's linear chain runs
off the right with no zoom-out. Mooer GE Labs offers unlimited drag-and-drop
effects and on an iPhone SE the amp labels are obscured by the effects below.

---

## Bindings

- **Learn-only.** "Stomp the switch you want", capture whatever arrives. Never a
  CC table as the primary path, never a vendor app.
- **Show the raw event verbatim** (`CC20 ch·any 127/0`, `Key 65545`) next to what
  it *means*. Those are two different facts.
- **Draw the binding on the thing it controls** — an inline `EXP` or `A` badge on
  the parameter, with the assigned window shaded on its own track. Quad Cortex has
  no "what does EXP1 control?" page; users open blocks one at a time to find out.
- **Resolve conflicts out loud, at bind time.** FabFilter documents a silent
  overwrite; Ableton silently deletes the first mapping. Say what's already bound.

---

## Antipatterns — each one shipped in a real product

- Drag-to-reorder sharing a gesture with scroll — *Anagram*
- Footswitch firing on RELEASE because hold is overloaded on the same switch;
  vendor advice is "press slightly earlier" — *Anagram*
- A hidden long-press as the only way to scope a parameter — *Quad Cortex*
- A perform view reachable only by an undiscoverable swipe — *Quad Cortex Gig View*
- Inactive footswitch LEDs fully off — *Quad Cortex*
- Scenes existing only as a footswitch mode, with no list — *HeadRush Prime*
- Momentary/latching scoped per SWITCH instead of per assignment — *Helix*
- Commands silently stacking on an existing assignment — *Helix Command Center*
- An app that won't launch without internet — *Nano Cortex / Cortex Cloud*
- Gapless switching regressing to ~60ms in a firmware update — *Nano Cortex*
- Double-tap as the only way to swap gear, with no UI hint — *Spark app*
- Amp labels obscured by the chain below on a small phone — *Mooer GE Labs*
- A tuner that silently discards unsaved edits — *Dimehead*
- Screen-reader inaccessibility with no vendor response — *Cortex Mobile*

---

## House rules that bind every screen

- The **bottom nav is the only persistent chrome**, and stays visible and
  functional on every screen.
- A screen has **at most one header row**, and only if it's functional.
- Overlays are **height-capped and scrollable**, and paint last.
- Colours come from the Hi-Fi palette: bg `#08070f`, hero radial to `#191430`,
  ink `#f2eee6`, accent `#ff4d00`. Libre Caslon Display for names, Work Sans for
  UI, uppercase micro-labels with wide letter-spacing.
