# Decision log (rolling)

Newest first. One line per decision, with the WHY. Add an entry whenever a
direction is chosen, reversed, or a constraint is discovered.

- **2026-08-15** PERFORM's STOMP mode (`StackPerformView::layoutGrid`)
  shows a fixed FS1-FS8 switch grid, resolving an ambiguity in the visual
  spec's "one switch per chain item that has an fs number" wording: read
  literally that would only produce switches for ASSIGNED slots, leaving
  nothing to tap for an unassigned one — but the same spec also requires
  an unassigned tap to toast ("Assign in EDIT → tap gear → FOOTSWITCH").
  Fixed 8 slots (matching FS1-8 physical footswitch numbers) is the only
  reading that satisfies both: each slot resolves to whichever chain item
  (if any) carries that `fs` number, "—" + toast when none does.
- **2026-08-15** Stack detail's PERFORM tab (Task 4) is live: on-screen
  SCENES/STOMP switch grid, no MIDI (separate future plan). Engine truth
  stays ONE model + ONE IR — a scene/AMP tap is audible only when the
  target toneId differs from what `AppShell` tracks as actually live
  (`liveModelToneId_`/`liveIrToneId_`, updated only by PERFORM's own
  applies); the bypass map + activeScene + the amp's activeChannel index
  are STORED-state writes that apply immediately regardless of the load
  outcome (Phase A has no multi-pedal DSP chain, so bypass is visual-only),
  with activeScene+activeChannel reverted together on a failed scene load.
  One `nam::ToneInfo` load in flight at a time; a tap mid-flight replaces
  the single pending slot (last tap wins) rather than queuing per-resource.
  Entering PERFORM (including re-entry via the setlist ‹ › arrows) applies
  the stack's active model/IR if not already live, so a resource that
  failed to load keeps retrying on each re-entry until it succeeds.
  Resolved an ambiguity in the visual spec: the setlist header's ‹ prev /
  next › are setlist navigation (`onPrevStack`/`onNextStack`), separate
  from a dedicated ≥44px ‹ exit chevron (`onExit`) that returns to EDIT —
  the spec's own open question #8 flagged the exit target as possibly too
  small/ambiguous, so this splits it into its own affordance rather than
  overloading prev. PERFORM hides the bottom nav entirely
  (`AppShell::setNavHidden`, an explicit stage-view exception): derived
  fresh from the target screen on every `show()` call plus set directly by
  `StackDetailScreen::onTabChanged`, so any exit path (tab switch, back,
  paywall, screen navigation) restores it without scattering restore calls.
  Back-button chain: paywall → tuner → Detail overlay → **PERFORM → EDIT**
  → Detail → Home → pop-to-Play (the new PERFORM step slots in right after
  the existing overlay-close check, ahead of the Detail→Home pop).
- **2026-08-15** Stack deletion now lives in Detail → EDIT's guided view: a
  REMOVE STACK row at the bottom opens an inline confirm ("Remove
  '{name}'?" / REMOVE / CANCEL); confirming erases from `AppShell::stackList_`,
  persists, and navigates back to Home. Closes the "no delete UI" MAJOR
  ledgered below (Task 2) — the binding visual spec still shows no delete
  control on Home itself, so deletion is Detail-only for now, matching the
  spec's own scope decision (`2026-08-14-stacks-redesign-phase-a-design.md`:
  "Stack deletion lives in Detail → EDIT as a REMOVE STACK row").
- **2026-08-15** Stacks Home replaces the fixed-6-slot `StacksScreen`
  accordion; `AppShell` now holds `std::vector<nam::Stack>` (v2 ordered-chain
  model via `StackModel`, v1 files migrate transparently on load, next save
  writes v2) instead of `StacksScreen::Stack`. Row tap navigates to a new
  `StackDetailScreen` (EDIT/PERFORM tabs, shell only this task) rather than
  expanding in place; `applyStack`/"LOAD" is dropped (superseded by
  PERFORM). Why: the new design's ordered/multi-amp/scene model has no
  fixed-slot equivalent — a rewrite, not a restyle (see
  docs/superpowers/specs/2026-08-14-stacks-redesign-notes.md).
- **2026-08-15** Stacks Home ships with NO stack-delete affordance (v1's
  per-row "×" has no replacement yet) — flagged MAJOR by the Task 2
  adversarial review since "+ NEW STACK" appends+persists unconditionally
  and, once `kSoftPaywall` flips on, `Entitlements::canSaveRig` counts
  every accidental create against the free-rig cap. Known and tracked, not
  fixed in Task 2: the binding visual spec itself has no delete control in
  the captured Home/Detail markup (notes doc "Open questions" #2) — where
  it lives (swipe-to-delete? Detail overflow?) is a design decision for a
  later task, not a Task 2 coding gap.
- **2026-08-14** `StackModel::ChainItem::gearTag` for the SPACES slot is
  `"space"` (singular), not `"spaces"`. Why: the Phase A plan
  (docs/superpowers/plans/2026-08-14-stacks-redesign-phase-a.md:42) and its
  Task 1 brief both specified `"spaces"`, which the adversarial reviewer
  already flagged as a MAJOR finding on the plan commit (f2d133b) — it
  contradicts the live TONE3000 API gear values (`Source/net/Tone3000Api.h`)
  and the shipped `StacksScreen::slotDefs()` (`{"SPACES","space"}`).
  Corrected in `StackModel`'s v1 migration before later tasks (the live
  gear picker) could bake `gears=spaces` into a request and inherit the bug.
- **2026-08-14** Pro gating became an .env build switch: `NAM_GATES_ENABLED=0`
  disables paywall/gates/lock-glyphs for development (CMake injects it, same
  pattern as the publishable key; .env changes now retrigger configure).
  Absent or 1 = gates ON — fail-safe for store builds. Re-enabling is on the
  internal-testing checklist. Why: day-to-day dev kept hitting the gates.
- **2026-08-14** Stripe + federated login (Google etc.) DEFERRED to the
  "app gets a backend" milestone (Cloud Sync or the Web/PWA version —
  whichever is green-lit first). Why: Play policy requires Play Billing
  for in-app digital goods; Stripe makes Chris the merchant of record
  (tax/refunds/backend/accounts) for ~$0.90 more per $9.99 sale; and
  federated login has no feature behind it while the app has no server —
  it would only degrade the "no data collected" privacy story (and
  obligate Sign in with Apple on iOS). Revisit both as ONE decision at
  that milestone; TONE3000 OAuth stays a service link, never app identity.
- **2026-08-14** Freemium Pro-unlock plan complete (26d2ae4..54bd747, 25
  commits): entitlements core, JUCE 9.0.1 + Play Billing 9.1.0, billing
  wrapper w/ offline cache, paywall sheet, gates (STACKS/layouts/demo
  tracks), release scaffold, legal docs, kSoftPaywall flag (committed
  internal/hard-gate). Final whole-branch review: internal-testing ready
  after Chris's gates (repo private, keystore, phone verification of
  JUCE 9 audio + live billing, TONE3000 email, NAM naming). Public-flip
  work list (do WITH the kSoftPaywall flip): defer restore's DoneFn to
  purchasesListRestored; gate rig-2+ EDITING for lapsed Pro
  (grandfathering spec); route live gates through Entitlements::
  canUseLayout/canUseDemoTrack; extract AppShellPro.cpp (AppShell.cpp
  ~1046 lines); "still waiting on Google Play…" state for pending
  purchases; re-check data-safety wording; hedge naming-note fair-use
  claim. Why ledgered here: the SDD workspace (gitignored) is deleted at
  plan completion — this list is the durable copy.
- **2026-08-13** License swapped from the GPLv3 plan to proprietary
  (Task 7): `LICENSE` now "Copyright © 2026 Chris Harper, all rights
  reserved" with a third-party-components note (NeuralAudio MIT,
  NeuralAmpModelerCore MIT, RTNeural BSD-3, math_approx BSD-3, Eigen MPL2,
  JUCE Personal tier, dr_wav public-domain/MIT-0, PicoSHA2 MIT,
  nlohmann/json MIT). Each entry was checked against what's actually
  compiled/linked (not just vendored) — the pre-push reviewer caught an
  initial pass that missed NeuralAmpModelerCore and math_approx despite
  both being unconditionally built into the NeuralAudio target
  (`extern/NeuralAudio/NeuralAudio/CMakeLists.txt`); fixed same-commit. A
  second review pass then caught Melatonin Inspector (MIT, Sudara
  Williams) missing too — fetched unconditionally on desktop (non-Android)
  builds regardless of build type, despite a stale "debug only" CMake
  comment; added to LICENSE. README's license section no longer itemizes
  third-party components at all (it now just points at `LICENSE`) so the
  two lists can't drift apart again. TONE3000 email and
  NAM-naming decision note drafted (`docs/business/tone3000-email-draft.md`,
  `docs/business/nam-naming-note.md`) — both unsent/undecided, Chris acts
  on them manually. **Repo-visibility change (public → private) is NOT
  done** — gated on Chris confirming in-session per the plan; still
  blocks the first store build per `docs/business/play-release-checklist.md`.
- **2026-08-13** Release engineering scaffolded (Task 6):
  `signingConfigs.release` in `Builds/Android/app/build.gradle` reads
  `NAMPLAYER_UPLOAD_STORE_FILE/STORE_PASSWORD/KEY_ALIAS/KEY_PASSWORD` from
  `~/.gradle/gradle.properties` and is a `hasProperty`-gated no-op when
  absent, so `assembleDebug`/`bundleRelease` both keep working on a machine
  with no keystore (verified: `bundleRelease` → BUILD SUCCESSFUL, unsigned
  `.aab`). Confirmed `-DCMAKE_BUILD_TYPE=RelWithDebInfo` (set once in
  `defaultConfig`) governs the native build for release too, not just
  debug — neither buildType overrides it. `versionName` moved to
  `1.0.0-internal.1` for the internal-testing track. Privacy policy
  (`docs/legal/privacy-policy.md`) and the Play Console runbook
  (`docs/business/play-release-checklist.md`) flag two open blockers before
  any *public* listing: the repo going private (Task 7, ordered before any
  store upload) and the "NAM" naming/trademark question — both fine to
  defer for internal testing.
- **2026-08-13** Pro lock-glyph state now seeded from the entitlement disk
  cache synchronously in `AndroidAudioApp::initBilling()`, right after the
  cache read and before the async `restoreProductsBoughtList` round trip
  (`AndroidBilling.cpp`). Previously the only `refreshProState()` call tied
  to service wiring ran *before* the cache seed, so a returning Pro user
  launching offline saw every gated row padlocked until (if ever) the store
  round trip corrected it. Found by task review of the freemium-gates
  commits (505a881/e02fcde); see build-deploy.md for the `nam_test` AVD
  quirk (no Play Services) that masks this bug on-device unless you
  hand-seed the cache and stub the billing call.
- **2026-08-13** JUCE 9.0.1 migration path taken (Task 2b, re-spike GO —
  docs/business/billing-spike.md): bumped `GIT_TAG` for both build trees.
  Headless suite (JUCE-free) unaffected; desktop target compiled with zero
  source changes; Android needed one fix (`NamLookAndFeel.cpp`'s
  `FontOptions().withTypeface(...)` now asserts in JUCE 9 — switched to the
  `FontOptions(Typeface::Ptr)` constructor). `juce::juce_product_unlocking`
  + `JUCE_IN_APP_PURCHASES=1` re-linked on Android alongside
  `com.android.billingclient:billing:9.1.0` (the version JUCE 9.0.1's
  bundled `JuceBillingClient.java` targets, per the Projucer Android
  exporter). Verified on-device: clean launch, no JNI abort, no assertion
  spam. Task 3 unblocked.
- **2026-08-13** Billing spike verdict: NO-GO on JUCE 8.0.15's stock
  Android IAP as pinned (docs/business/billing-spike.md). Its precompiled
  `JuceBillingClient` targets Play Billing Library 7.0.0 — already past
  Google's v8+ floor — but bumping the Gradle dependency to v8 breaks that
  same precompiled shim at runtime (calls the no-arg
  `enablePendingPurchases()`, removed in v8). Linking the module with no
  billing jar present also crashes every launch (JUCE's JNI classes
  resolve eagerly at `System.loadLibrary` time, not lazily). JUCE `9.0.1`
  (tagged 2026-08-10) carries the GPB 9.1.0 fix; path decision (JUCE 8→9
  migration vs. hand-rolled JNI shim) pending before Task 3.
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
