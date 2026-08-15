# Decision log (rolling)

Newest first. One line per decision, with the WHY. Add an entry whenever a
direction is chosen, reversed, or a constraint is discovered.

- **2026-08-15** Stacks UX-correctness batch (5 STILL-OPEN checklist items):
  amp stomp tap now routes to `applyAmpCycle` instead of the meaningless
  bypass toggle (plus a bonus fix found while E2E-verifying it: the STOMP
  grid's amp cell was showing the item's static title, not the active
  channel's, so the label never visibly changed on tap even once the cycle
  itself was firing correctly); FS pills hidden on a Cab's item sheet
  (nothing acts on a cab footswitch); `StackGearPicker`'s disabled-tab
  treatment generalized from two hardcoded Add-mode bools to an
  owner-supplied `std::array<bool,4>` + hint string, so AddChannel ("pick
  an amp capture for this channel") and Swap ("swap must stay the same
  gear type") now dim the tabs that can't apply instead of silently
  dropping a mismatched pick; the mid-load failure toast is now gated on
  the same `stillValid` check its neighbors (`onFail`, save/push) already
  used, so removing a stack mid-load no longer toasts about a rig that's
  gone. **Wizard nav policy**: chose discard-on-nav-away (closing the
  wizard, same as hardware back) over hide-the-nav-while-open — the hide
  approach would need `setNavHidden` wired at 3+ scattered call sites
  (wizard open, its own internal back-chevron/onCancel, and
  `handleBackButton`'s existing close), the exact shape of bug the
  paywall's `dismissPaywall()` unification fixed earlier; discard is one
  line in `AppShell::show()` (the single choke point every nav button
  routes through) reusing the same `closeWizard()` hardware back already
  calls, so both exits agree on one policy instead of introducing a second.
  E2E on-device (emulator-5554): amp-mapped switch cycles audibly with a
  live logcat load and a title that now updates; Cab sheet has no FS row,
  pedal/amp still do; AddChannel and Swap both show the dimmed tabs + hint;
  a stack removed mid-load (network blocked via `iptables` to force a
  genuine in-flight window) produces no orphan toast after the parked
  request resolves, no crash; wizard opened then dismissed via a nav
  button leaves no draft behind on returning to STACKS. Headless suite
  green throughout; Android arm64-v8a RelWithDebInfo build clean.
  **Review round 1** (adversarial, VERDICT PASS, no BLOCKER) caught two
  MAJORs in this same batch, fixed same-commit: (1) `applyAmpCycle`'s
  `(prevChannel+1) % channels.size()` is a no-op at `size()==1`, so a
  switch mapped to a single-channel amp went from "toggles a meaningless
  LED" to "does visibly nothing at all" (title/LED unchanged, plus a
  wasted `stacks.json` write) -- `onStompTap` now toasts "only one channel
  — add more in EDIT" instead of calling the cycle when
  `channels.size() <= 1`. (2) `finishToneLoad`'s `stillValid` (index AND
  uid match at that exact index) is the right predicate for `onFail`/
  save/push, which index `stackList_[stackIdx]` directly and would
  corrupt a DIFFERENT stack shifted onto that index by an unrelated
  earlier removal -- but gating the failure toast on the same strict
  predicate (this batch's item-4 fix) meant a stack that merely shifted,
  not vanished, silently lost its failure toast too. Split into
  `stillValid` (strict, still gates the mutations) and a looser
  `stillExists` (uid found anywhere in `stackList_`) that gates the toast
  alone. Two MINORs fixed alongside: the Add-mode picker hint said "one
  amp per stack for now" even when only CAB was dimmed (now names
  whichever tab(s) are actually disabled); the STOMP cell's amp sub-label
  now falls back to the item's own title when `channels` is empty (a
  hand-edited/future-version-file case, unreachable from any in-app flow
  today) instead of painting blank.
  **Review round 2** (same-day, VERDICT PASS, no BLOCKER) re-scrutinized
  the round-1 "cab fs not zeroed" MINOR and escalated it to MAJOR: it's a
  data-migration bug, not a UI gap -- a stacks.json written by the PRIOR
  build (before the sheet stopped offering cab FS pills) can already hold
  `{"type":"cab","fs":4}` on a real device, and nothing migrated it, so
  the slot was permanently unclearable. Fixed with a TDD test first
  (`tests/test_stack_model.cpp`, red before the fix): `StackModel::
  itemFromJson` now zeroes `fs` for any parsed `GearType::Cab` item --
  parse() is the one choke point every persisted file (v1 or v2) flows
  through, so this closes the gap for hand-edited files too, not just the
  UI-reachable case. Also fixed a MINOR from the same pass: `onStompTap`'s
  single-channel guard tested the TAPPED amp but `applyAmpCycle` cycled
  the FIRST amp in the chain -- divergent only for a hand-edited/future
  multi-amp file (canAdd caps Add at one amp today), but cheap to close:
  `applyAmpCycle` takes an optional `targetUid` (empty = first-amp,
  preserving the SCENES-mode AMP cell's behavior; STOMP now passes its own
  uid). Two more round-2 MINORs logged, not fixed: a toast/no-revert
  mismatch when an unrelated EARLIER stack is removed mid-scene-load
  (pre-existing baseline behavior, not a regression from this batch); and
  the nav-bar's few px of dead space beside the orb falling through to
  `show(Screen::Play)` now also discards an open wizard with no confirm
  (same fallback existed pre-batch, this batch's discard-on-nav-away
  policy just added a consequence to it) -- both are real but small
  enough to track rather than block on.
  **Review round 3** (same-day, VERDICT PASS, no BLOCKER/MAJOR) stress-
  tested the round-2 fixes against a hand-edited multi-amp/uid-less-item
  file and found the remaining gap is real but the reviewer's own read was
  "file for the next batch": `applyAmpCycle`'s `targetUid` overloads empty
  `juce::String` as both "use first amp" and "a legitimate item uid" (an
  item with no `"uid"` key parses to `""`, since `assignMissingStackUids`
  only mints STACK uids, never item ones) -- closing it fully means
  minting item uids at parse the same way, a bigger change than this
  batch's scope. Also flagged: cycling a non-Phase-A-reachable second amp
  would desync the STOMP label from the single-model engine truth on the
  next re-apply (same "unreachable via `canAdd`'s one-amp cap" carve-out);
  the legacy cab-fs zeroing (round 2) is silent to the end user with no
  release-note surfacing; `AppShellPerform.cpp` sits at 396/400 lines
  (no violation, flagged for the next feature landing there); and the
  `applyAmpCycle`/`targetUid` branch itself is untested (unreachable
  in-app, so neither the headless suite nor a device run exercises it --
  accepted under the JUCE-UI-has-no-harness carve-out). None fixed;
  tracked here as the reviewer suggested rather than in a fourth round.
- **2026-08-15** Pre-launch hardening batch landed (9df1dc9..8a759c5, 5
  commits): `Stack.uid` replacing every (index,name) async revalidation
  with (index,uid) (TDD, `StackModel::nextStackUid`/`assignMissingStackUids`
  mint monotonic "sN" ids on parse + at every creation site);
  `stacks.json.bak` safety net for a stacks.json `StackModel::parse`
  couldn't make sense of; a 30s `performApplyInFlight_` watchdog
  (`AppShell::ApplyTimeout`, same owner-backed one-shot-`juce::Timer`
  shape as `AndroidAudioApp::PurchaseTimeout`, declared AndroidAudioApp.h,
  implemented in AndroidBilling.cpp) so a `svc_.loadTone`
  callback that never fires can't wedge PERFORM forever; and
  `AppShell::dismissPaywall()` unifying the three paywall-dismiss sites
  (only `onClose` re-derived nav-hidden before). Adversarial review round 1
  on the uid + backup commits caught one MAJOR worth recording: the backup
  trigger `stackList_.empty() && raw.isNotEmpty()` couldn't tell "parse()
  degraded unreadable content to {}" from "parse() correctly read a
  legitimately empty, well-formed file" (e.g. the user deleted their last
  rig) — the latter would silently overwrite a real recovery `.bak` with
  nothing worth recovering on the very next ordinary save. Fixed via new
  `StackModel::looksLikeStacksFile` (top-level shape check only: valid
  JSON, v1 array or v2-object-with-version-2 — independent of how many
  stacks it actually yields) so the trigger reads "unrecognized shape,"
  not "zero stacks." Two MINOR uid-safety findings fixed alongside it:
  `maxStackUidIndex` signed-overflowed (UB) on a hand-edited uid of
  exactly `"s2147483647"` (INT_MAX) — capped at a 1e9 sane ceiling; and
  `assignMissingStackUids` left hand-edited duplicate uids uncorrected,
  letting the (index,uid) revalidation accept the wrong stack after a
  removal shifted indices — every occurrence past the first is now
  re-minted. Residual, MINOR, hand-edit-only-boundary-value gaps the
  reviewer flagged and were judged not worth chasing given the effort/
  reachability trade: the duplicate re-mint isn't re-checked against
  already-seen uids at the exact 1e9 cap boundary (a fresh duplicate needs
  uids in the 999,999,999–1,000,000,000 range, i.e. a billion-stack file);
  `nextStackUid` can likewise mint a uid colliding with an existing
  cap-excluded one at that same boundary; and `looksLikeStacksFile`
  narrows the backup net for a v2 file whose top-level shape is valid but
  EVERY stack entry is individually fatal to parse (implausible from
  organic corruption — truncation and a version bump both still trigger
  the backup). E2E: uid migration/persistence/removal-stability/creation-
  minting verified against the real device's existing 11-stack library
  (`s1`..`s11` minted on load, stable across a removal, `s12` minted
  correctly past the gap on the next wizard save); paywall open (STACKS
  nav gate, `NAM_GATES_ENABLED=1`) → Android back-button dismiss → nav
  bar intact verified on-device, `.env` restored after. Full native
  library compiles AND links clean against the Android arm64-v8a
  toolchain, android-29 platform (matching `minSdk`) — not just the
  desktop target, which doesn't compile the Stacks UI layer at all.
  Watchdog's 30s-timeout path (bogus id + network off) was judged too
  time-costly to stage via adb for this pass and is the one item of the
  original brief's E2E ask not device-verified; the code path was
  reviewed twice adversarially with no BLOCKER/MAJOR survivors instead.
- **2026-08-15** Stacks Redesign Phase A COMPLETE (f2d133b..c40b881, 19
  commits): ordered-chain StackModel (v2 json, v1 migration), Home
  (setlist chips), Detail EDIT (guided + freeform, live picker, item
  sheet, REMOVE STACK), Detail PERFORM (on-screen scenes/stomp, nav-hidden
  stage view, local-first applies), Create wizard (templates + 4-step
  flow, scene seeding). Final whole-branch review verdict: ready for
  internal testing after the two Criticals landed (stale live-ids across
  Play↔PERFORM; two-slot pending apply split). **Pre-launch checklist**
  (fix before public flip; the SDD workspace is deleted at completion —
  this is the durable copy). **DONE 2026-08-15** in the hardening batch
  (9df1dc9..aac1000, see the newer entry above): Stack.uid; stacks.json.bak;
  performApplyInFlight_ watchdog; dismissPaywall() factoring. **DONE
  2026-08-15** in the UX-correctness batch (see the newer entry above):
  picker mismatched-tab toast in AddChannel/Swap modes; wizard nav policy;
  amp stomp tap = bypass LED (now routes to channel cycle); hide FS pills
  on Cab item sheet; suppress failure toast after stack removal. **STILL
  OPEN:** PERFORM tuner anchor (Play-layout placement); hoist scrim/sheet
  hex to NamLookAndFeel (0xa008070f/0xf214101f, old + new sites); PEDALS
  wrapping-grid vs spec horizontal-strip needs Chris's ratification;
  backport PressRegion machine to StackPerformView; stacks.md v1 header
  rewrite. Deferred phases unchanged: MIDI/foot-control plan, dual-amp
  A/B/stereo DSP, audible pedals, Play-screen mock deltas.
- **2026-08-15** Critical fix: wizard-built stacks store a `LibraryEntry`
  id (filename, e.g. `keep_75774.nam`) in `ChainItem::toneId` by design —
  they're inherently local (`LibraryEntry` has no TONE3000 tone id field).
  PERFORM's apply path (`AppShell::startToneLoad`) now routes by id kind
  instead of changing what the wizard stores: a new `findLocalEntry` checks
  the id against the local library (models via `getModels_`, IRs via
  `getIrs_`, picked by `tone.format`) before touching `svc_.loadTone`; a
  match applies synchronously via `loadModel_`/`loadIr_` and resolves the
  SAME completion/pending-drain machinery a network load would, so the
  in-flight/pending state never desyncs. No match (real TONE3000 numeric
  id, or a local id whose entry was since deleted) falls through to the
  existing `svc_.loadTone` route unchanged. The two id spaces can't
  collide: `LibraryEntry::id` is always `keep_<id>.nam` / `ir_<id>.nam`,
  never a bare TONE3000 id.
- **2026-08-15** Stacks Phase A final-review fix round: (1) `AppShell::show()`
  clears `liveModelToneId_`/`liveIrToneId_` whenever Play becomes the nav
  target, since only PERFORM's own applies updated them before and a
  Play-side load (loadModel_/loadIr_/setCab) could leave a stale id that a
  later PERFORM re-entry trusted instead of re-validating. (2) PERFORM's
  single `pendingPerformApply_` slot split into `pendingModelApply_` +
  `pendingIrApply_` (keyed by `tone.format`), both drained on load
  completion — one slot let a mid-flight IR request silently clobber a
  parked model request (or vice versa) during a fast NEXT/‹ › or scene tap.
  (3) Wizard save (`StackCreateWizard::doSave` → new `seedScenes()`) now
  seeds one Scene per amp channel (name = channel title or "Scene {n}",
  pedalBypass mirrors the as-built all-on state, `activeScene = 0`) so
  PERFORM's SCENES grid isn't empty from creation — interim until a real
  scene editor exists. Corrected 2026-08-15 (fix round 2): the original
  version of this entry claimed template picks (`pickTemplate`) already
  defined their own scenes and were left untouched — false, the three
  `StackTemplates.h` literals set no `.scenes` at all, so a template pick
  hit the exact same empty-SCENES symptom; `pickTemplate` now seeds its
  cloned stack too, via a shared `seedScenesFor` free function.
- **2026-08-15** Stack creation wizard (Task 5, final Phase A task) ships:
  step-0 template gallery (Plexi Crunch/Modern Metal/Clean Platform, data-
  only `StackTemplates.h`, cloned with fresh `nextUid` sequence on pick,
  no toast) + steps 1-4 guided build, hosted as a screen-level child of
  `StacksHomeScreen` (`StackCreateWizard`, own back chevron; owner wires
  it via `wizard()`/`closeWizard()` the same way Detail exposes
  `picker()`/`itemSheet()`) rather than a new `AppShell::Screen` — this
  kept `AppShell.cpp` to +8 lines (the mandatory `handleBackButton` hook,
  ordered before the Detail-overlay chain per the task brief) instead of
  touching its show()/resized()/target-selection logic at all. `nam::
  LibraryEntry` carries no gear-type tag (only Model vs Ir), so steps 1
  ("amp channels") and 2 ("pedals") both list the full kept-models set
  with no way to filter one to "pedals only" — flagged in the task report
  as a follow-up (a real gear tag on `LibraryEntry`), not fixed here.
  FS numbers 1-4 (A-D) are written onto `ChainItem::fs` by the wizard's
  own arm-then-assign flow, reusing the existing field rather than adding
  new model surface — the same field PERFORM's STOMP grid already reads,
  so a wizard-assigned switch is immediately live in PERFORM with no
  extra wiring, at the cost of "channel cycle" actions reading as a plain
  bypass toggle in STOMP mode today (no MIDI layer exists yet to give
  that assignment its own semantics — tracked as a Phase A gap, not
  addressed here). `onSave(nam::Stack, bool toast)` deviates from the
  brief's literal `onSave(nam::Stack)` so the owner can skip the "Saved ·
  A-D mapped" toast on a template pick, matching the spec's silence on
  that path.
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
