# M-Vave Chocolate Plus — device reference

Chris **owns this unit** (acquired 2026-08-15). It is the first hardware
target for the foot-control feature; the architecture direction lives in
[controllers.md](controllers.md), and no MIDI layer exists in the app yet.

Sources are vendor listings, the manuals.plus manual pages, and the
vguitarforums Chocolate/Chocolate Plus thread (links at the bottom). **Where
this page says REPORTED, treat it as unverified** — sources disagree on the
factory defaults, and the unit in hand is the only authority. See "Verify on
the real unit" for how to settle each one in about ten minutes.

## What it is

Four-switch (A/B/C/D) programmable MIDI foot controller, metal chassis,
~215×40.5×38 mm, 285 g. Rechargeable 300 mAh battery, DC 5 V / 45 mA,
~2.5 h charge for ~12 h use. Three-digit display showing the current bank
and the PC/CC value being sent.

Two more switches are **combinations**, not extra hardware: **E = A+B**
pressed together, **F = C+D**. Bank changes are also a simultaneous
left/right press.

## Connectivity (all three at once is the selling point)

- **Bluetooth LE MIDI** — the path NAM Player will use first. Advertises as
  **`FootCtrl`** (this is what shows up when scanning, including in the
  CubeSuite app). Pairing needs no button ritual: power it on and the BT LED
  flashes until something connects. The flashing indicator can be disabled
  with a startup button combination.
- **USB** — both directions. As a **device** it's the wired MIDI path; the
  Plus additionally has a **USB HOST** port so it can drive an effects unit
  directly with no computer. Host mode needs the rear switch set to `H`
  **and** B+C pressed together to disable Bluetooth (they can't both own the
  MIDI stream).
- **3.5 mm TRS** — doubles as MIDI out and the expression-pedal input.
  Output is **3.3 V nominal**. Note the base Chocolate used a 1/4" jack
  here; the Plus's 3.5 mm is reported to be **less accommodating of some
  expression pedals**, so an adapter plus a range check is expected.

## Operating modes

- **Program-change banks** — Mode A sends PC 0–31, Mode B sends PC 0–127,
  Mode C sends CC 0–127, all organised in banks.
- **Custom** — per-switch CC assignment, each switch independently
  **momentary or toggle**.
- **Advanced Custom** (firmware v10+) — arbitrary MIDI messages per switch,
  including **SysEx entered as hex**.
- **Mixed mode** (firmware v11+) — each switch independently MIDI / computer
  keyboard / page-turner, across six groups.
- Bank capacity is advertised as **16 groups, 64 total**.
- Supported message types: **PC, CC, Note On/Off, SysEx**.

## Factory defaults — REPORTED, verify before relying on any of it

- MIDI channel **1**.
- Switches A/B/C/D → **CC 20 / 21 / 22 / 23**. One source instead describes
  the defaults as named CC functions (Bank Select / Modulation / Breath /
  Undefined), and another says there are no documented factory assignments
  at all and everything must be programmed. **Three sources, three answers**
  — which is exactly why the app must not hardcode any of them.
- Combination switches: E sends "control function 2, value 5"; F sends
  "control function 3, value 5" (as-quoted, and not obviously meaningful
  without seeing the wire).

## Firmware history worth knowing

v10 added Advanced Custom, v11 added Mixed mode, v12 fixed v11 bugs. There
are **reports of units bricking during the v11+ upgrade**, and Android 14
tablets reportedly flash more reliably than older Android. Treat firmware
updates on Chris's unit as a deliberate, backed-up decision, not routine.

## What this means for NAM Player

- **Bind what it sends; never require CubeSuite.** The defaults are
  contested and user-editable, so the app's MIDI-learn ("press the switch
  you want for next tone") is the design that survives contact with real
  hardware. This is already the direction locked in
  [controllers.md](controllers.md); this page is the evidence for it.
- **BLE MIDI first** — Android's MIDI API covers BLE and USB MIDI, and JUCE
  wraps it, so this unit needs no custom protocol work (unlike the Spark
  Control, which is proprietary BLE). Scan for `FootCtrl`.
- **Expression is just a CC.** Auto-calibrate the range in-app ("rock
  through full range") rather than trusting a nominal 0–127 sweep — the
  3.5 mm jack's pedal compatibility is the known-weak part of this unit.
- **Momentary vs toggle is the pedal's setting, not ours.** The app should
  behave correctly either way: treat a CC as "pressed" on any value change
  it's bound to, and let the binding decide whether it's a trigger or a
  state.
- **SysEx config is a stretch goal, not a dependency.** The Plus accepts
  SysEx and a partial community mapping exists
  (github.com/cbix/mvave-chocolate-sysex); configuring the pedal *from* the
  app would be a delight, but the feature must work without it.

## Verify on the real unit (do this before writing the MIDI spec)

Pair it to the phone and log what actually arrives — that settles every
REPORTED item above at once:

1. Power on; confirm it advertises as `FootCtrl` and pairs from Android's
   Bluetooth settings as a MIDI device.
2. With a MIDI monitor app (or a throwaway debug build logging raw MIDI),
   record: the channel, the exact message for each of A/B/C/D, whether it's
   momentary or toggle out of the box, what E and F send, what a bank change
   sends, and the CC + value range of the expression jack through its full
   sweep.
3. Write the observed values back into this page, replacing the REPORTED
   block, and note the firmware version shown on the display.

Until step 3 happens, this page is research, not truth.

## Sources

- [manuals.plus — Chocolate Plus manual pages](https://manuals.plus/asin/B0DSFWSD9M)
  (also `B0F8VRKM6K`, `B0DJ3JKC7N`)
- [vguitarforums — Chocolate & Chocolate Plus for MIDI control](https://www.vguitarforums.com/smf/index.php?topic=38822.0)
  (best technical thread: mode list, TRS voltage, host-mode combo, firmware
  history)
- [Guitarbites — using the Chocolate with other pedals](https://guitarbites.com/how-to-use-the-m-vave-cuvave-chocolate-wireless-midi-footswitch-pedal-with-other-pedals/)
  (pairing name, pairing-on-power-up behaviour)
- Vendor listings (Amazon B0DNZTNKP1 / B0GM8KYWTK) for physical, battery,
  bank-count and message-type claims.
