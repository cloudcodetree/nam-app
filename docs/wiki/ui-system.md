# UI system

## Design language ("Hi-Fi", from Chris's Claude Design mockups)

Deep purple-black (`col::bg` 0xff08070f), burnt-orange accent (0xffff4d00),
Libre Caslon display serif + Work Sans UI, faders not knobs, pill buttons,
glowing tone cards. All colours via `nam::ui::col` — no magic hex outside
`NamLookAndFeel.h`. Meters: lime = input, blue (`meterBlue`) = output.

## Anatomy (current truth)

- **Nav**: BROWSE / FAVORITES | status orb | STACKS / ⋯ (vector icons +
  micro-labels). DOWNLOADED lives in the ⋯ menu. Orb = input arc (lime,
  from 9 o'clock), output arc (blue, from 3 o'clock), latency ms + LATENCY
  label centre; tap opens the flyout (ENGINE rate/buffer pills, INPUT/OUTPUT
  rows w/ pickers + MUTE, TEST TONE).
- **Play deck**: view-type button (strip) picks swipe cards / detail list /
  2-col grid / 4-col grid. Cards: art aspect-fit on black over a 50% blurred
  self-backdrop; ✓ save + ♥ heart top-left, mixer flip button top-right,
  circled chevrons straddle the edges; tap flips to QUICK SETTINGS. List
  rows tap-expand inline with DEMO AUDIO + PAIR dropdowns. NO pagination —
  browse appends pages as the deck end nears (`onDeckEndReached`).
- **Strip**: view title left; view-type + gear dropdown (browse) + Filters
  pill pushed right. Filters open a grouped chip flyout.

## House rules (bugs behind each one)

- Every flyout/dropdown/overlay is height-capped and scrollable, never full
  screen; overlays paint LAST (dots once painted over the filters flyout).
- Non-ASCII glyphs only via `juce::String::fromUTF8` (raw literals render
  mojibake on Android); prefer vector `juce::Path` icons — the Android
  colour-emoji fallback ignores `setColour` (the heart had to become a path).
  Correct UTF-8 encoding is necessary but NOT sufficient: Work Sans itself
  has no glyph for some codepoints (found with "▸" U+25B8 -- correctly
  encoded via `fromUTF8`, still rendered as a bare dot on Android, no tofu
  box) and the OS font-fallback chain doesn't reliably fill the gap either.
  If a glyph looks wrong/missing on-device despite `fromUTF8` being used,
  suspect font coverage next, not encoding — `drawFwdTriangle`
  (`StackWidgets.h`) is the fix pattern: draw the shape as a `juce::Path`
  instead of trusting the typeface has it.
- `drawImage*` inherits the current colour's ALPHA as opacity — set
  `g.setOpacity(1.0f)` after painting translucent washes (thumbnails once
  rendered at 4%).
- Touch = press/drag/tap state machine; scroll gestures must never select.
  Empty-rect hit tests: `empty.expanded(k)` is a live region near origin.
- JUCE 8: `Font::getStringWidth` is gone → `GlyphArrangement::getStringWidth`.

## DI test-track tray (2026-08-20)

`DiTrayPanel` + `AppShellDiTray.cpp`. Slides down from the TOP over every
screen so a DI loop can be auditioned against the current tone from anywhere.

- **Three states, deliberately.** Idle = a grab tab only. Collapsed = a slim
  status strip that exists ONLY while audio plays. Expanded = 62%-capped,
  scrolling. The middle state is what keeps the app from carrying a second
  header row on every screen (CLAUDE.md allows one).
- Idle/collapsed **inset** the host screen through `contentBounds()`; expanded
  floats over it. Nothing ever covers the bottom nav.
- Plays through `svc_.playDemoLive` (live engine, real-time inference), NOT a
  pre-rendered slot, so it reflects the tone currently loaded. Guitar input is
  muted for the duration, as audition does.
- 34 tracks; the first 4 are bundled in the APK and the rest carry a `GET` tag
  because they fetch once and cache.
