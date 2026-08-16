# NAM Player Wiki — Hub

The rolling knowledge base: one hub, topic spokes. **Keep it rolling** — when
a decision lands or hard-won knowledge surfaces, update the relevant spoke in
the same commit (or add a dated line to [decisions.md](decisions.md)).

## Spokes

| Spoke | What lives there |
|---|---|
| [architecture.md](architecture.md) | Layer map, JUCE-free core, screens/services pattern |
| [realtime-audio.md](realtime-audio.md) | Audio-thread rules, publish-then-retire, reclamation, metering |
| [ui-system.md](ui-system.md) | Hi-Fi design system, nav, card anatomy, overlays, deck views |
| [tone3000.md](tone3000.md) | API facts, OAuth/PKCE, search params, keep/save semantics |
| [stacks.md](stacks.md) | Stacks feature: slots, apply rules, persistence |
| [controllers.md](controllers.md) | Foot controllers: transport-agnostic control layer, hardware research |
| [chocolate-plus.md](chocolate-plus.md) | M-Vave Chocolate Plus device reference (Chris owns one) |
| [build-deploy.md](build-deploy.md) | Android build, devices, adb-qr, clang tools, tests |
| [review-gate.md](review-gate.md) | Hooks pipeline: sanitize → format → adversarial review → auto-push |
| [decisions.md](decisions.md) | Rolling decision log (dated, one entry per decision, with WHY) |

## Ground rules for the wiki

- A spoke states **current truth**, not history — history lives in
  [decisions.md](decisions.md) and git.
- Every entry says **why**, not just what.
- If you learned it the hard way (a reviewer finding, a device-only bug, an
  API surprise), it belongs here — the next session shouldn't re-learn it.
- Spokes stay short; split a spoke before it becomes a god page (same rule
  as code files).
