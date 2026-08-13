# Billing spike — go/no-go on `juce_product_unlocking` (Android)

Task 2 of the freemium/Pro-unlock plan. Spike scope: wire
`juce::juce_product_unlocking` into the Android CMake target, confirm it
builds, and determine whether JUCE's bundled Android in-app-purchase
implementation can actually ship against Google's current Play Billing
Library requirement.

**Original verdict (Task 2): NO-GO on JUCE 8.0.15's stock Android IAP path,
as pinned then.** Task 3 was told not to proceed on the assumption that
`juce::InAppPurchases` works out of the box on Android. See "Path forward"
below for the options weighed at the time.

**Superseded by Task 2b: GO on JUCE 9.0.1.** The app now builds on JUCE
`9.0.1` with `juce::juce_product_unlocking` linked and
`com.android.billingclient:billing:9.1.0` on the Gradle classpath — see
"JUCE 9.0.1 re-spike (Task 2b) — GO" below for what changed and the
on-device evidence. The NO-GO narrative and "Path forward" section are kept
below as the historical record of why the upgrade was needed.

**The CMake change from this spike (linking `juce::juce_product_unlocking`
+ `JUCE_IN_APP_PURCHASES=1`) has been reverted.** An earlier draft of this
doc left it wired in on the theory that it "costs ~0.27 MB and zero
functionality" since nothing calls `juce::InAppPurchases` yet. That
premise was wrong and the pre-push adversarial review caught it: see
"Startup-crash finding" below. `CMakeLists.txt`'s Android target currently
links neither the module nor the define.

## What was done

- `CMakeLists.txt` (Android target): added, built, verified, then
  **reverted** `juce::juce_product_unlocking` /
  `JUCE_IN_APP_PURCHASES=1` — see "Startup-crash finding" for why.
- Built via `cd Builds/Android && JAVA_HOME=... ./gradlew assembleDebug`
  with the module linked. **Result: BUILD SUCCESSFUL.** The module links
  and compiles cleanly; no source changes were needed elsewhere. (A build
  pass only proves it compiles — see below for why that's not the same as
  the app running.)
- Probed the "override the Gradle billing dependency version" patch option
  named in the task brief (see below) — attempted, and it also builds
  successfully, which is itself part of the finding: the break this spike
  found does **not** surface at build time.

## Bundled billing library version vs. Google's current minimum

**JUCE 8.0.15 (our pinned `GIT_TAG`) targets Play Billing Library 7.0.0.**
Evidence:
- `extras/Projucer/Source/ProjectSaving/jucer_ProjectExport_Android.h:939` —
  the Projucer Android exporter's hardcoded dependency line:
  `implementation('com.android.billingclient:billing:7.0.0')`.
- `modules/juce_product_unlocking/native/java/app/com/rmsl/juce/JuceBillingClient.java`
  uses the v6/v7-era API surface (`QueryProductDetailsParams`,
  `queryPurchasesAsync(QueryPurchasesParams, ...)`, no-arg
  `enablePendingPurchases()`) — consistent with a v7.0.0 target, not v8+.
- This Java source is **not** compiled by our build. JUCE ships it as a
  pre-compiled, gzip'd DEX blob embedded directly in
  `juce_InAppPurchases_android.cpp` (`juceBillingClientCompiled[]`,
  loaded via `DECLARE_JNI_CLASS_WITH_BYTECODE`). We cannot recompile it
  against a different Billing Library version without patching JUCE itself.

**Google's current minimum: Billing Library 8, effective now.** Per the
official deprecation page
([developer.android.com/google/play/billing/deprecation-faq](https://developer.android.com/google/play/billing/deprecation-faq)):
by **August 31, 2026** (extension to November 1, 2026 available), all new
apps and app **updates** must use Billing Library 8 or later. Today is
August 13, 2026 — 18 days out. Version 7's own two-year grace window is
what's expiring; v8's runs to August 31, 2027, and the current latest is
v9 (May 2026). NAM Player hasn't shipped this feature yet, so by the time
freemium/Pro-unlock ships, v8+ will not be optional — it will already be
the enforced floor for any update.

## Why "just override the Gradle dependency version" doesn't work

The brief's suggested patch option was tried: add
`implementation "com.android.billingclient:billing:8.0.0"` to
`Builds/Android/app/build.gradle` and rebuild.

- **The build still succeeds.** `assembleDebug` → BUILD SUCCESSFUL with the
  override in place. This is expected and is part of the problem: JUCE's
  C++ talks to `JuceBillingClient` entirely through JNI method-ID lookups
  at runtime (`GetMethodID`/reflection), not compile-time Java linking, so
  neither CMake/clang nor Gradle/javac ever check the embedded DEX
  bytecode's method calls against whatever Billing Library jar actually
  ends up on the runtime classpath.
- **It is very likely broken at runtime.** Per Google's own migration guide
  ([developer.android.com/google/play/billing/migrate-gpblv8](https://developer.android.com/google/play/billing/migrate-gpblv8)),
  Billing Library 8 **removes the no-argument `enablePendingPurchases()`**
  overload (replaced by `enablePendingPurchases(PendingPurchasesParams)`).
  That is the *exact* call JUCE 8.0.15's bundled, precompiled
  `JuceBillingClient` makes at construction
  (`native/java/app/com/rmsl/juce/JuceBillingClient.java:50`,
  `billingClient = BillingClient.newBuilder(context).enablePendingPurchases()...`).
  Swapping the runtime jar to v8 (or v9) without recompiling that class
  would surface as a `NoSuchMethodError`/JNI method-not-found the first
  time `juce::InAppPurchases` is constructed on Android. This wasn't
  reproduced on-device — no purchase flow exists yet to trigger
  construction, and doing so is out of this spike's scope — but the
  breaking-change list and the exact method signature match are Google's
  own documentation, not inference.
- Net effect: **7.0.0 (works, fails Google's policy gate) and 8.0.0+
  (satisfies Google's policy, breaks JUCE's precompiled JNI shim) are
  mutually exclusive** for JUCE 8.0.15 as pinned. There is no Gradle-only
  version number that satisfies both.

Because the failure mode is a silent runtime crash rather than a build
error, the Gradle override was **reverted** — `Builds/Android/app/build.gradle`
is unchanged from before this spike. Shipping the override as-is would look
green in CI and break on-device.

## JUCE 9.0.1 re-spike (Task 2b) — GO

Task 2 ended NO-GO on JUCE 8.0.15 and named option 1 (upgrade to `9.0.1`)
the front-runner. Task 2b executed that upgrade as its own migration, then
re-ran this spike's pass condition (link the module, add the matching
Billing Library jar, launch on-device) against the new tree.

**Migration, Step 1-3 (`CMakeLists.txt` `GIT_TAG 8.0.15` → `9.0.1`):**

- Both build trees (desktop CMake preset, Android Gradle/CMake) re-fetch
  JUCE at `9.0.1` (commit `e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8`,
  2026-08-10).
- The headless suite (`nam_tests`) is JUCE-free by design (CLAUDE.md
  invariant) and was unaffected — 102 cases / 379,266 assertions green,
  unchanged from before the bump.
- The desktop `NamPlayer` target compiled **with zero source changes**.
- The Android `NamPlayer` target needed exactly **one fix**:
  `Source/app/ui/NamLookAndFeel.cpp`'s `displayFont()`/`uiFont()` built
  fonts via `juce::FontOptions().withTypeface(typeface)`. JUCE 9's
  `FontOptions::withTypeface()` now asserts `style.isEmpty()`, and a
  default-constructed `FontOptions()`'s style is `"Regular"` (non-empty) —
  so every call fired a `JUCE Assertion failure in juce_FontOptions.h:138`
  (visible in `adb logcat`, dozens of times per frame, though not fatal
  under RelWithDebInfo/NDEBUG). Fixed by switching to the
  `FontOptions(Typeface::Ptr)` constructor, which sets `name`/`style` from
  the typeface directly and never round-trips through the asserting
  `withTypeface()` overload. Confirmed via `adb logcat`: assertion spam
  gone after the fix, app renders identically (screenshot evidence below).
- This was built and launched (Step 2/3) **before** touching billing at
  all, to isolate "does the JUCE 9 migration itself work" from "does
  billing work on JUCE 9" — it does; the Hi-Fi UI renders correctly with
  no startup crash.

**Re-spike, Step 4 (billing re-added):**

- `CMakeLists.txt`'s Android target now links `juce::juce_product_unlocking`
  and defines `JUCE_IN_APP_PURCHASES=1` (previously reverted — see
  "Startup-crash finding" above).
- `Builds/Android/app/build.gradle` now has a `dependencies` block adding
  `implementation('com.android.billingclient:billing:9.1.0') { exclude
  group: 'org.jetbrains.kotlin', module: 'kotlin-stdlib-jdk7'/'jdk8' }` —
  **9.1.0**, not 8.x, found by grepping the JUCE 9.0.1 checkout's own
  Projucer Android exporter
  (`extras/Projucer/Source/ProjectSaving/jucer_ProjectExport_Android.h:940`:
  `implementation('com.android.billingclient:billing:9.1.0')`), i.e. the
  exact version JUCE's own project generator would have wired in for this
  tag. This matches the "GPB 9.1.0" support referenced in commit
  `1b58549d6c`'s message.
- Confirmed directly against the JUCE 9.0.1 checkout that the runtime break
  this spike originally found is actually fixed, not just newer:
  `modules/juce_product_unlocking/native/java/app/com/rmsl/juce/JuceBillingClient.java`
  now calls
  `BillingClient.newBuilder(context).enablePendingPurchases(PendingPurchasesParams.newBuilder().enableOneTimeProducts().build())`
  — the modern parameterized overload — not the no-arg
  `enablePendingPurchases()` that Billing Library 8+ removed and that
  crashed 8.0.15's precompiled shim. (This Java file is still not compiled
  by our build — JUCE embeds it as a precompiled DEX blob in
  `juce_InAppPurchases_android.cpp`, same as before — but its *source*,
  which the embedded bytecode was built from, now targets the version we're
  shipping.)
- The vendored Java shell (`Builds/Android/app/src/main/java/com/rmsl/juce`)
  did **not** need re-vendoring: `JuceBillingClient.java` was never part of
  the vendored tree (it ships precompiled), and the app builds/launches
  identically with the existing vendored files — JUCE 9 relocated some
  upstream native-java sources into new subdirectories
  (`native/javacore/...`, `native/javaopt/...` replacing the JUCE-8-era
  `native/java/app/...` layout) but that's a source-tree reorg on JUCE's
  side, not an API JuceActivity.java/Java.java must expose differently; our
  two local patches (performance-guard code, removed
  appOnResume/appNewIntent calls) remain untouched and the build proves
  they still link.
- **Build:** `assembleDebug` → BUILD SUCCESSFUL with the module, define, and
  Gradle dependency all in place. Per-entry APK inspection (`zipfile`)
  confirms the billing library actually packaged:
  `classes3.dex` (8,070,412 bytes) plus billing resource entries
  (`res/xml/com_android_billingclient_phenotype.xml`,
  `res/raw/com_android_billingclient_registration_info.binarypb`, etc.) —
  consistent with the +2.6 MB estimate from the original spike's probe.
- **Launch (the actual pass condition — eager JNI resolution against a real
  classpath):** installed and launched on `emulator-5554`
  (`com.namplayer.app/com.rmsl.juce.JuceActivity`). Process stayed alive
  (`adb shell pidof` returned a pid after launch, not empty), the Hi-Fi UI
  rendered correctly (screenshot evidence), and `adb logcat -d` after
  launch shows **no** `FATAL EXCEPTION`, no `AndroidRuntime` crash trace, no
  `JNI DETECTED ERROR`, and no billing-related error — the eager
  `DECLARE_JNI_CLASS` resolution for all `com/android/billingclient/api/*`
  classes (the exact mechanism that aborted every launch on 8.0.15 with no
  jar present) now succeeds because the jar is on the classpath. This is
  the on-device verification the original spike's "Path forward" section
  said was still owed (a build pass alone was not sufficient evidence
  before, and still wouldn't be — this time the launch was actually
  checked).

**Verdict: GO for Task 3.** `juce::InAppPurchases` is linked with a
policy-compliant (Billing Library 9.1.0, well above Google's v8 floor),
JNI-compatible (JUCE 9.0.1's rebuilt `JuceBillingClient`) billing library on
Android, verified by an actual on-device launch rather than a build pass.
Task 3 can build real purchase-flow code (`InAppPurchases::getInstance()` —
note JUCE 9 also made `InAppPurchases` a singleton, a further breaking
change from 8.0.15's constructible-object API, per JUCE's
`BREAKING_CHANGES.md`) against this tree. Exercising an actual purchase
call end-to-end (not just constructing/resolving the JNI classes) is still
Task 3's job, not this migration's — this re-spike's scope was the
migration + confirming eager JNI resolution no longer aborts, both done.

## Startup-crash finding: linking the module *without* a billing jar also crashes

The first version of this spike shipped the CMake change (module linked,
no Gradle dependency added) on the theory that it was inert — "nothing
calls `juce::InAppPurchases` yet, so it can't do anything at runtime."
That's wrong, and the pre-push adversarial reviewer caught it before it
reached `main`. The mechanism, verified directly against our pinned JUCE
8.0.15 checkout (`build/_deps/juce-src`):

1. `JUCE_IN_APP_PURCHASES=1` compiles
   `juce_product_unlocking/native/juce_InAppPurchases_android.cpp` into
   `libjuce_jni.so` (confirmed empirically in this spike — the .so grew
   277 KB, including the embedded DEX).
2. That translation unit declares **12 namespace-scope `DECLARE_JNI_CLASS`
   statics** for `com/android/billingclient/api/*` (lines 45–125) plus one
   for `JuceBillingClient` (line 737). `DECLARE_JNI_CLASS` expands to a
   `static inline` instance whose constructor registers itself in
   `JNIClassBase::getClasses()` — that registration runs unconditionally
   at static-init time, not lazily when something constructs
   `InAppPurchases`.
3. JUCE's mandatory Android startup path, `com.rmsl.juce.Java.initialiseJUCE`,
   calls `JNIClassBase::initialiseAllClasses()`
   (`juce_Threads_android.cpp:71-75`), which resolves **every** registered
   class — including all 13 billing classes — before any app UI runs, on
   every launch, on every device (our `minSdk 29` is well above whatever
   floor would skip this).
4. `Builds/Android/app/build.gradle` has no `dependencies` block — no
   Billing Library jar ships in the APK. For each billing class:
   `FindClass` fails (the class genuinely isn't on the classpath), the
   embedded `JuceBillingClient` DEX loader can't help either (it
   implements billing-library interfaces it can't resolve, and
   `jassert`-based guards are compiled out under our RelWithDebInfo/NDEBUG
   build), and the pending exception is never cleared before the next JNI
   call.
5. The next call — `GetMethodID` on a null/invalid `jclass` — is either
   caught by ART's runtime check (`JniAbort`, "JNI DETECTED ERROR IN
   APPLICATION") or, since the debug build has `debuggable true`
   (forcing CheckJNI on), aborts even earlier on the pending exception.
   Either path: **native `abort()` during `initialiseJUCE`, on every
   launch, with zero user interaction** — not something a purchase flow
   has to be wired up to trigger.

This wasn't caught in the original pass because verification stopped at
`assembleDebug` → BUILD SUCCESSFUL; the app was never actually launched
after the CMake change. A single install-and-tap on the emulator would
have shown it. **Fix applied: the module link and
`JUCE_IN_APP_PURCHASES=1` were reverted from `CMakeLists.txt` entirely**
rather than gated behind an off-by-default option — the spike's own
verdict is NO-GO, so there's no scenario in this repo, today, where the
module should be linked without a resolved billing-library version to go
with it. Whichever path forward is chosen re-adds both together.

## JUCE upstream status

**A fix is already tagged and available today — verified directly against
JUCE's git history, not the forum.** `git log`/`git merge-base
--is-ancestor` against `build/_deps/juce-src` (after `git fetch --tags
origin`) confirms:

- Tag **`9.0.1`** = commit `e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8`,
  dated **2026-08-10** (3 days before this spike).
- It contains commit `1b58549d6c` — *"InAppPurchases: Update Android
  implementation to support GPB 9.1.0"* (2026-07-27) — and commit
  `bf11446d82` — *"Android: InAppPurchases: Handle purchases that become
  PURCHASED when the BillingClient isn't running"* (2026-07-31). Both
  confirmed ancestors of `9.0.1` via `git merge-base --is-ancestor`.
  Neither is an ancestor of `9.0.0` (2026-07-21) or our pinned `8.0.15`.

So the earlier framing — "JUCE's `develop` branch has the fix, no tagged
release does" — was stale by the time of writing. `9.0.1` is a real,
tagged, pinnable release that carries GPB 9.1.0 support. It's a **major
version bump** from our current `8.0.15` (JUCE 8 → 9), so adopting it is
its own migration, not a one-line `GIT_TAG` swap — see path forward below.

## APK size

AGP's debug packaging (zip-flinger incremental patching) reserves slack
space in the APK zip container, so the outer `.apk` file's total byte size
does **not** reliably reflect content changes between incremental builds —
confirmed directly: linking a library that measurably grew
`lib/arm64-v8a/libjuce_jni.so` still left the packaged `app-debug.apk`
identical in total size in one comparison. Per-entry sizes (via Python's
`zipfile`, which reports true stored/compressed sizes per entry) are
reliable and were used instead:

- **Linking `juce::juce_product_unlocking` alone** (this spike's actual
  CMake change, no Gradle dependency): `lib/arm64-v8a/libjuce_jni.so` grew
  from 22,489,792 → 22,767,504 bytes stored, **+277,712 bytes (+0.27 MB)**.
  This is pure JNI glue + the embedded precompiled `JuceBillingClient` DEX
  blob; it ships in the APK regardless of whether a Billing Library Gradle
  dependency is ever added.
- **Adding `com.android.billingclient:billing:8.0.0` on top** (reverted
  probe, not shipped): added a new `classes3.dex` (2,564,352 bytes) plus
  ~40 KB growth in the main `classes.dex` and a few small resource entries
  (`billing.properties`, `res/xml/com_android_billingclient_phenotype.xml`,
  etc.) — **≈ +2.6 MB** of actual Billing Library + Kotlin-stdlib payload,
  unminified (this is a debug build; R8 in a release build would shrink
  this, but by an unknown amount without measuring a release build).

Both configurations above were built and measured, then fully reverted
(see "Startup-crash finding"); these numbers are a preview of cost, not a
description of what's currently in the tree.

## Path forward (not GO for Task 3 as-is)

Pick one before Task 3 proceeds with real purchase-flow code:

1. **Upgrade JUCE `8.0.15` → `9.0.1`** (tagged 2026-08-10, contains the
   GPB 9.1.0 Android rework — commits `1b58549d6c` and `bf11446d82`,
   verified above). This is **available today**, not a "wait for" item,
   but it's a major-version bump (JUCE 8 → 9) for a framework this app
   depends on for its entire UI/audio layer, not just billing — it needs
   its own migration pass (API/behavior diffs across the whole JUCE
   surface, not only `juce_product_unlocking`) and its own re-spike:
   rebuild, link the module, add the matching
   `com.android.billingclient:billing:9.x` Gradle dependency, and this
   time **launch the app and exercise an actual purchase call on-device**
   to confirm the eager-JNI-resolution path succeeds instead of aborting.
2. **Track JUCE's `develop` branch directly** (unpinned/untagged) instead
   of a tagged release. Not recommended now that `9.0.1` exists — there's
   no reason to trade a pinned tag for a moving target when the fix is
   already tagged.
3. **Hand-roll the JNI/Java billing shim** against Billing Library 8 or 9
   directly (bypass `juce_product_unlocking`'s Java layer; write and
   compile our own small Java class that talks to the modern
   `BillingClient` API, called from C++ via our own JNI glue), staying on
   JUCE 8.0.15. This is the "hand-rolled JNI addendum spec" the task brief
   anticipated as the fallback if this spike came back NO-GO — still an
   option if the JUCE 9 migration turns out to be too large to take on
   just for this feature. Larger scope than this spike; would need its
   own design doc.

Given `9.0.1` is already tagged, option 1 is the front-runner, but "just
bump the tag" undersells it — treat it as its own scoped migration task,
not a rider on Task 3.

**Nothing from Task 2's spike shipped enabled at the time this section was
written.** `CMakeLists.txt`'s Android target linked neither
`juce::juce_product_unlocking` nor `JUCE_IN_APP_PURCHASES=1` (reverted —
see "Startup-crash finding" above), and `Builds/Android/app/build.gradle`
had no billing-library dependency. Linking the module alone was a startup
crash, and no Billing Library version available under JUCE 8.0.15 (7.0.0 or
8.0.0+) was simultaneously policy-compliant and JNI-compatible with its
precompiled shim. **Task 2b (see "JUCE 9.0.1 re-spike" above) took option 1
from the list above, and both are now linked/enabled on JUCE 9.0.1**, with
on-device verification that the eager-JNI-resolution abort this section
describes no longer reproduces.
