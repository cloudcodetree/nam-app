# Billing spike — go/no-go on `juce_product_unlocking` (Android)

Task 2 of the freemium/Pro-unlock plan. Spike scope: wire
`juce::juce_product_unlocking` into the Android CMake target, confirm it
builds, and determine whether JUCE's bundled Android in-app-purchase
implementation can actually ship against Google's current Play Billing
Library requirement.

**Verdict: NO-GO on JUCE 8.0.15's stock Android IAP path, as pinned today.**
Task 3 should not proceed on the assumption that `juce::InAppPurchases`
works out of the box on Android. See "Path forward" below.

## What was done

- `CMakeLists.txt` (Android target): added `juce::juce_product_unlocking` to
  `target_link_libraries(NamPlayer PRIVATE ...)` and
  `JUCE_IN_APP_PURCHASES=1` to `target_compile_definitions`.
- Built via `cd Builds/Android && JAVA_HOME=... ./gradlew assembleDebug`.
  **Result: BUILD SUCCESSFUL.** The module links and compiles cleanly; no
  source changes were needed elsewhere.
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

## JUCE upstream status

The JUCE forum confirms this is known and already being addressed upstream,
just not in a release we can consume yet:
- JUCE added Billing Library 7.0.0 support in June 2024 (matches our
  8.0.15 tag).
- On **July 29, 2026** (2 weeks before this spike), JUCE's `develop` branch
  (unreleased) was updated: "InAppPurchases: Update Android implementation
  to support GPB 9.1.0." No tagged release includes this yet.

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

## Path forward (not GO for Task 3 as-is)

Pick one before Task 3 proceeds with real purchase-flow code:

1. **Wait for / adopt a tagged JUCE release with GPB 9.x support**, then
   bump the `GIT_TAG` in `CMakeLists.txt`'s `FetchContent_Declare(JUCE ...)`
   and re-run this spike (rebuild, add the matching Billing Library Gradle
   dependency, and this time exercise an actual purchase call to confirm
   at runtime). Track
   [github.com/juce-framework/JUCE/releases](https://github.com/juce-framework/JUCE/releases).
2. **Track JUCE's `develop` branch directly** (unpinned/untagged) to get
   the GPB 9.1.0 fix now. Not recommended: it trades the reproducibility of
   a pinned release tag for an unstable moving target, for a framework this
   app depends on for its entire UI/audio layer, not just billing.
3. **Hand-roll the JNI/Java billing shim** against Billing Library 8 or 9
   directly (bypass `juce_product_unlocking`'s Java layer; write and
   compile our own small Java class that talks to the modern
   `BillingClient` API, called from C++ via our own JNI glue). This is the
   "hand-rolled JNI addendum spec" the task brief anticipated as the
   fallback if this spike came back NO-GO. Larger scope than this spike;
   would need its own design doc.

The `CMakeLists.txt` change from this spike (module linked,
`JUCE_IN_APP_PURCHASES=1`) is left in place — it costs ~0.27 MB and zero
functionality on its own (nothing calls `juce::InAppPurchases` yet), and
whichever path above is chosen still needs the module linked. What's
**not** in place, deliberately, is a Gradle billing-library dependency —
adding one today would either violate Google's policy (7.0.0) or crash at
runtime (8.0.0+).
