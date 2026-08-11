# Architecture

## Layers

```
Source/dsp, Source/model, Source/net   JUCE-FREE core (headless-tested, TDD)
Source/app/ui                          shared JUCE UI (all platforms)
Source/app                             desktop host (MainComponent et al)
Source/app/android                     Android host (AndroidAudioApp)
Builds/Android                         Gradle shell + vendored JUCE java
```

- The JUCE-free boundary is deliberate: it's what makes the headless test
  suite possible and keeps a future WASM/web port feasible.
- `dsp::ToneEngine` chain: in-gain → gate → NAM model → IR cab → EQ →
  delay → reverb → out-gain. ONE model + ONE impulse at a time.

## UI composition

- `AppShell` owns every screen, the bottom nav (BROWSE / FAVORITES | status
  orb | STACKS / ⋯), the orb flyout (ENGINE/IO/TEST TONE), the ⋯ menu, and
  the tuner overlay. Screens are pure presentation; data and actions arrive
  through injected `std::function` services (`BrowseServices` etc.).
- `PlayScreen` is the hub: deck of tones as swipe cards / detail list /
  grids, filters, card flip to quick settings. Deck DATA is pushed by
  AppShell (`setDeckItems`); PlayScreen never fetches.
- Screens live in their own .h/.cpp; overflow work goes to sibling TUs
  (`PlayScreenDeck.cpp`, `AppShellDeck.cpp`) — see the no-god-files rule.

## Service pattern

Hosts (AndroidAudioApp / MainComponent) wire lambdas into
`AppShell::BrowseServices`. UI code must null-check every service and
re-validate captured indices/ids in async callbacks (decks change while a
fetch is in flight — capture ids, compare on completion).
