# Decision log (rolling)

Newest first. One line per decision, with the WHY. Add an entry whenever a
direction is chosen, reversed, or a constraint is discovered.

- **2026-08-13** Launch model adopted (business-advisor assessment,
  docs/business/2026-08-13): single $9.99 Pro unlock at launch; à-la-carte
  begins only when Looper ships as SKU #2; **first-rig-free** soft paywall
  at public launch (hard gate OK for internal testing); tier-picker, bundle,
  7-day trial, Cloud Sync all cut; MIDI foot control = Pro (free-basic-MIDI
  idea rejected); "own v1" kept as internal economics, never store copy.
  Time-sensitive: TONE3000 partnership email now; resolve "NAM" naming
  before any public impression.
- **2026-08-13** Multi-controller support direction locked (wiki
  controllers.md): transport-agnostic control layer; BLE/USB MIDI first
  (Chocolate Plus), Spark proprietary-BLE adapter second, HID third.
  Foot-control v1 design approved (ControlMap JUCE-free + MIDI transport +
  learn-by-doing panel).
- **2026-08-12** Freemium direction: free app + one-time $9.99 Pro unlock
  (no ads — ad SDKs fight the RT audio path and the audience pays for
  tools). **TONE3000-parity rule**: anything the site offers free is free
  in the app; Pro gates app-native only (Stacks, layouts, extra DI tracks,
  future MIDI/looper). The rule killed the planned 10-save cap. JUCE
  Personal tier, repo to go private pre-release, internal-testing launch
  bar. Spec: docs/superpowers/specs/2026-08-12-freemium-pro-unlock-design.md;
  audit skill: /tone3000-parity.
- **2026-08-11** Structure cleanup: dead screens deleted (~2,400 lines —
  Edit/Browse/Library/Live/AudioSettings/Placeholder had no nav entry);
  god files split into purpose-named TUs (AppShellChrome, AndroidTone-
  Services, AndroidAudioAudition); service lambdas take const&. Why: the
  codebase should satisfy its own CLAUDE.md rules, and every RT BLOCKER
  had been hiding in the two biggest files. Next step when desktop parity
  matters: promote the Android service TUs into shared classes.
- **2026-08-11** Async snapshots re-validate against live truth: browse
  appends carry a generation token; expanded-row tap rects recompute from
  current scroll; retire lists get a stagnation fallback for the stopped-
  device case. Why: three reviewer MAJORs shared the same root cause —
  state captured at one moment, consulted at another.
- **2026-08-11** Pagination retired entirely (dots + page arrows): replaced
  by infinite append — browse fetches the next page when the user nears the
  deck end (scroll or swipe). Why: with view types + scrollable lists the
  dots were vestigial, and the API is plain page-numbered REST (no SSE).
- **2026-08-11** TDD is the rule in the JUCE-free core; device verification
  (emulator/phone) is the E2E bar for UI flows. Why: the JUCE layer has no
  unit harness; the core does, and test-first caught the reclamation edge.
- **2026-08-11** RT buffer hand-off = raw atomic pointer + message-thread
  shared_ptr owners + block-gated retire. Why: libc++ shared_ptr atomics
  lock; count-based reclamation has no happens-before (both were BLOCKed).
- **2026-08-11** Commit messages carry no AI attribution (commit-msg hook
  strips it). Why: Chris wants project-voiced history.
- **2026-08-11** clang-format is law (JUCE spacing in ui/, compact core);
  clang-tidy advisory. One-time reformat in `.git-blame-ignore-revs`.
- **2026-08-10** Coding standards codified in CLAUDE.md: no god files
  (new ≤400 lines; 800+ must extract), publish-then-retire, async
  re-validation, bounded caches. Reviewer enforces them.
- **2026-08-10** Adversarial review gates every push; commits auto-push
  (background). Why: solo dev + high-risk RT code needs an always-on second
  pair of eyes; the gate's first day proved the value.
- **2026-08-10** phase5a-android merged to main; work continues ON main.
- **2026-08-10** Nav: BROWSE/FAVORITES | orb | STACKS/⋯ (DOWNLOADED in ⋯);
  top bar retired; ENGINE config moved into the orb flyout. Why: Play is
  the hub, chrome minimal, audio config one tap from the orb.
- **2026-08-10** Stacks load tones on the fly from TONE3000 (no pre-
  download); engine truth = one model + one impulse decides apply rules.
- **2026-08-10** Saved vs favorited split on ONE library list via the
  `favorite` flag: heart = download+save+flag, download = save only.
- **2026-08-10** Output meter taps the device write, not engine telemetry
  (engine freezes when bypassed); output = blue, input = lime.
- **2026-08-10** Explicit USB claim both sides + buffer clamp to device min:
  BT-hijack immunity + the real latency fix (~14–16 ms).
- **2026-08-05** Vendored JUCE Gradle shell (Approach A) for Android;
  RelWithDebInfo mandatory for native.
- **2026-08-03** GPLv3 public repo; JUCE free tier; PKCE public client
  embeds only the publishable key.
