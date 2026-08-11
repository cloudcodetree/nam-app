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
