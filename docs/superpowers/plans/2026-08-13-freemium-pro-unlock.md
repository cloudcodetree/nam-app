# Freemium Pro Unlock Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship the single $9.99 Pro unlock (spec 2026-08-12, revised 2026-08-13) to the Play internal-testing track: entitlements, billing, paywall, gates, release plumbing, and the legal/launch checklist items that block it.

**Architecture:** JUCE-free `nam::Entitlements` policy class (headless-tested) → host billing wrapper around `juce_product_unlocking` with a cached-but-never-granting offline state → house-style paywall overlay → gates in AppShell/StacksScreen. Internal testing ships the hard STACKS gate; the first-rig-free soft paywall swaps in before public launch (Task 8).

**Tech Stack:** JUCE 8.0.15 (`juce_product_unlocking`), Google Play Billing (bundled by the module — Task 2 verifies the version), Catch2 headless tests, existing hooks pipeline.

## Global Constraints

- TONE3000-parity rule (CLAUDE.md): saves/downloads/favorites/browse/audition are NEVER gated.
- clang-format before every commit (pre-push blocks); no AI attribution in commit messages (hook strips).
- New files ≤400 lines; files >800 lines must not grow — new logic goes in new TUs.
- RT rule: nothing in this plan touches the audio thread; billing/gating is message-thread only.
- Android native builds RelWithDebInfo; every new `.cpp` registered in the CMake target that uses it.
- Product id: `pro_unlock` (non-consumable). Price set in Play Console, never hardcoded — UI shows the store-returned localized price.
- Commits auto-push through the adversarial gate; check `.git/autopush.log` after each.

---

### Task 1: nam::Entitlements (JUCE-free, TDD)

**Files:**
- Create: `Source/model/Entitlements.h` (header-only, like `Source/dsp/Delay.h`)
- Test: `tests/test_entitlements.cpp`
- Modify: `tests/CMakeLists.txt` (add the test source to the `nam_tests` target's source list, alongside `test_librarystore.cpp`)

**Interfaces:**
- Produces: `nam::Entitlements` with `void setPro(bool)`, `bool isPro() const`, `bool canSaveRig(int existingRigs) const`, `bool canUseLayout(int layoutMode) const`, `bool canUseDemoTrack(int index) const`, `static constexpr int kFreeRigs = 1`, `static constexpr int kFreeDemoTracks = 1`. Tasks 3/5/8 consume exactly these names.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_entitlements.cpp
#include <catch2/catch_all.hpp>
#include "model/Entitlements.h"

TEST_CASE("Entitlements: free tier gates app-native features only") {
    nam::Entitlements e;
    REQUIRE_FALSE(e.isPro());
    // First rig free forever; second requires Pro.
    REQUIRE(e.canSaveRig(0));
    REQUIRE_FALSE(e.canSaveRig(1));
    REQUIRE_FALSE(e.canSaveRig(5));
    // Swipe-card layout (0) free; list/grids (1..3) gated.
    REQUIRE(e.canUseLayout(0));
    REQUIRE_FALSE(e.canUseLayout(1));
    REQUIRE_FALSE(e.canUseLayout(3));
    // First demo track free; the rest gated.
    REQUIRE(e.canUseDemoTrack(0));
    REQUIRE_FALSE(e.canUseDemoTrack(1));
}

TEST_CASE("Entitlements: pro unlocks everything") {
    nam::Entitlements e;
    e.setPro(true);
    REQUIRE(e.isPro());
    REQUIRE(e.canSaveRig(99));
    REQUIRE(e.canUseLayout(3));
    REQUIRE(e.canUseDemoTrack(33));
}

TEST_CASE("Entitlements: revoke re-locks without deleting anything") {
    nam::Entitlements e;
    e.setPro(true);
    e.setPro(false);
    REQUIRE_FALSE(e.isPro());
    // Rigs beyond the free one are read-only-locked by callers; the policy
    // class only answers canSaveRig — there is deliberately NO API to cap
    // or delete saves (TONE3000-parity regression pin).
    REQUIRE_FALSE(e.canSaveRig(2));
    REQUIRE(e.canSaveRig(0));
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build --preset default --target nam_tests 2>&1 | tail -5`
Expected: compile FAIL — `model/Entitlements.h` not found.

- [ ] **Step 3: Minimal implementation**

```cpp
// Source/model/Entitlements.h
#pragma once

namespace nam {

// Monetization policy (JUCE-free). TONE3000-parity rule: there is NO API
// here for gating saves/downloads/favorites — only app-native features.
// Spec: docs/superpowers/specs/2026-08-12-freemium-pro-unlock-design.md
class Entitlements {
public:
    void setPro(bool pro) { pro_ = pro; }
    bool isPro() const { return pro_; }

    // First rig is free forever; more require Pro.
    bool canSaveRig(int existingRigs) const { return pro_ || existingRigs < kFreeRigs; }
    // Layout 0 = swipe cards (free); 1..3 = list/grids (Pro).
    bool canUseLayout(int layoutMode) const { return pro_ || layoutMode == 0; }
    // DI audition tracks: first free, rest Pro.
    bool canUseDemoTrack(int index) const { return pro_ || index < kFreeDemoTracks; }

    static constexpr int kFreeRigs = 1;
    static constexpr int kFreeDemoTracks = 1;

private:
    bool pro_ = false;
};

} // namespace nam
```

- [ ] **Step 4: Run tests**

Run: `cmake --build --preset default --target nam_tests && ./build/tests/nam_tests "[Entitlements]*" ; ./build/tests/nam_tests | tail -2`
Expected: all cases PASS, full suite still green.

- [ ] **Step 5: clang-format + commit**

```bash
clang-format -i Source/model/Entitlements.h tests/test_entitlements.cpp
git add Source/model/Entitlements.h tests/test_entitlements.cpp tests/CMakeLists.txt
git commit -m "feat: Entitlements policy class (first rig free, layouts/demo tracks Pro)"
```

---

### Task 2: Billing spike — go/no-go on juce_product_unlocking

**Files:**
- Modify: `CMakeLists.txt` (Android target: link `juce::juce_product_unlocking`, add `JUCE_IN_APP_PURCHASES=1` to `target_compile_definitions`)
- Create: `docs/business/billing-spike.md` (findings)

**Interfaces:**
- Produces: a build with `juce::InAppPurchases` available, and a WRITTEN verdict: bundled Play Billing Library version, whether it meets Google's current minimum (search "Play Billing Library version requirements" — deprecation policy requires a recent major), and GO (Task 3 proceeds) or NO-GO (stop; a hand-rolled JNI addendum spec is required before continuing).

- [ ] **Step 1: Wire the module**

In `CMakeLists.txt` Android target: add `juce::juce_product_unlocking` to `target_link_libraries(NamPlayer PRIVATE ...)` and `JUCE_IN_APP_PURCHASES=1` to `target_compile_definitions`.

- [ ] **Step 2: Build**

Run: `cd Builds/Android && JAVA_HOME=/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home ./gradlew assembleDebug 2>&1 | grep -E "error|BUILD"`
Expected: BUILD SUCCESSFUL. If the module drags in Java/Gradle billing deps, note what it adds to the APK.

- [ ] **Step 3: Determine the bundled Billing Library version**

Run: `grep -rn "billingclient\|billing" build/_deps/juce-src/modules/juce_product_unlocking --include="*.java" --include="*.gradle" --include="*.cpp" -l | head` then inspect hits; also `grep -rn "com.android.billingclient" Builds/Android/app/build.gradle build/_deps/juce-src -r | head`. Record the version. Web-search Google's current minimum ("Google Play Billing Library deprecation schedule").

- [ ] **Step 4: Write the verdict**

`docs/business/billing-spike.md`: module version found, Google minimum, APK size delta, GO/NO-GO. If NO-GO: state the patch option (override the billing dependency version in the Gradle build) and whether it was attempted.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt docs/business/billing-spike.md Builds/Android/app/build.gradle
git commit -m "chore: billing spike — juce_product_unlocking wired, version verdict recorded"
```

---

### Task 2b: JUCE 9.0.1 migration + billing re-spike (added after Task 2's NO-GO)

**Context:** Task 2 found JUCE 8.0.15's Android IAP unshippable (embedded
shim targets Play Billing 7.0.0; Google's floor is v8+; Gradle-only bumps
crash at runtime). JUCE 9.0.1 (tagged 2026-08-10, same licensing/price as
JUCE 8 — $0) contains the GPB 9.1.0 update (commit 1b58549). Chris chose
the upgrade path. Reference: docs/business/billing-spike.md.

**Files:**
- Modify: `CMakeLists.txt` (`FetchContent_Declare(JUCE ... GIT_TAG 9.0.1)`; re-add `juce::juce_product_unlocking` + `JUCE_IN_APP_PURCHASES=1` to the Android target ONLY after the migration builds)
- Modify: `Builds/Android/app/build.gradle` (add the Play Billing dependency version JUCE 9.0.1 expects — find it the way Task 2 did: grep the JUCE 9 checkout's Projucer Android exporter / module docs for `com.android.billingclient:billing:`)
- Modify: whatever `Source/` files JUCE 9 API changes break (fix minimally, matching surrounding style; clang-format each)
- Create: append a "JUCE 9.0.1 re-spike" section to `docs/business/billing-spike.md`

**Interfaces:**
- Produces: a tree on JUCE 9.0.1 where (1) the FULL headless suite passes, (2) the Android app builds AND launches on the emulator without crashing (adb screenshot as evidence), (3) `juce::InAppPurchases` is linked with the billing jar present so Task 3 can build on it. Desktop target must still configure (build it if the environment allows; at minimum `cmake --preset default` configures).

- [ ] **Step 1: Bump the tag, build headless tests, fix breakage until green** (`cmake --preset default --target nam_tests`; full suite must pass)
- [ ] **Step 2: Android build without billing** (confirm the migration alone is clean)
- [ ] **Step 3: Emulator launch + screenshot** (app renders; no startup crash)
- [ ] **Step 4: Re-add billing module + define + matching Gradle billing dependency; rebuild; emulator launch again** (eager JNI now resolves against the shipped jar — THIS is the re-spike's pass condition)
- [ ] **Step 5: Document in billing-spike.md (GO verdict for Task 3 if step 4 passed), update docs/wiki/decisions.md (path chosen: JUCE 9.0.1), clang-format, commit** (`chore: JUCE 9.0.1 migration — billing re-spike GO`)

---

### Task 3: Host billing wrapper (AndroidBilling.cpp)

**Files:**
- Create: `Source/app/android/AndroidBilling.cpp` (member functions of `AndroidAudioApp`, new TU — same pattern as `AndroidToneServices.cpp`)
- Modify: `Source/app/android/AndroidAudioApp.h` (members + method decls below)
- Modify: `Source/app/android/AndroidAudioApp.cpp` (ctor: wire services into shell; call `initBilling()` after `setBrowseServices`)
- Modify: `CMakeLists.txt` (register the new TU)

**Interfaces:**
- Consumes: `nam::Entitlements` (Task 1); `juce::InAppPurchases` (Task 2).
- Produces (consumed by Tasks 4/5): on `AndroidAudioApp` — `nam::Entitlements entitlements_;` (message-thread only), `void initBilling();`, `void purchasePro(std::function<void(bool, juce::String)> done);`, `void restorePurchases(std::function<void(bool, juce::String)> done);`, `static juce::File entitlementCacheFile();`, `void persistEntitlement(bool pro);`. AppShell service struct (Task 4) receives `isPro()`, `purchasePro`, `restorePurchases` lambdas.

- [ ] **Step 1: Header members**

In `AndroidAudioApp.h` private section:

```cpp
    // Pro entitlement (message thread only). Play is the source of truth;
    // the cache file is a fallback for offline launches, never a grant.
    nam::Entitlements entitlements_;
    void initBilling();
    void purchasePro(std::function<void(bool, juce::String)> done);
    void restorePurchases(std::function<void(bool, juce::String)> done);
    static juce::File entitlementCacheFile();   // appdata "NAM Player/entitlement.json"
    void persistEntitlement(bool pro);
    std::function<void(bool, juce::String)> purchaseDone_;   // in-flight callback
    struct BillingListener;                                  // defined in AndroidBilling.cpp
    std::unique_ptr<juce::InAppPurchases::Listener> billingListener_;
```

Add `#include "model/Entitlements.h"` and `#include <juce_product_unlocking/juce_product_unlocking.h>` to the header includes.

- [ ] **Step 2: Implementation TU**

```cpp
// Source/app/android/AndroidBilling.cpp
#include "app/android/AndroidAudioApp.h"

// Pro-unlock billing (split TU per no-god-files rule). Message thread only.
// Cache semantics: seed from disk at startup, then let the store overwrite
// in BOTH directions — cached Pro without store confirmation is honored
// (offline flight), but a store answer of "not owned" clears it.

static constexpr const char* kProProductId = "pro_unlock";

juce::File AndroidAudioApp::entitlementCacheFile() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("NAM Player/entitlement.json");
}

void AndroidAudioApp::persistEntitlement(bool pro) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("pro", pro);
    const auto f = entitlementCacheFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText(juce::JSON::toString(juce::var(obj)));
}

struct AndroidAudioApp::BillingListener : juce::InAppPurchases::Listener {
    explicit BillingListener(AndroidAudioApp& o) : owner(o) {}
    AndroidAudioApp& owner;

    void purchasesListReceived(const juce::Array<juce::InAppPurchases::PurchaseInfo>& purchases,
                               bool success) override {
        if (!success) return;   // keep cached state on query failure
        bool owned = false;
        for (const auto& p : purchases)
            if (p.purchase.productId == kProProductId) owned = true;
        owner.entitlements_.setPro(owned);
        owner.persistEntitlement(owned);
        if (owner.shell_ != nullptr) owner.shell_->refreshProState();
    }

    void productPurchaseFinished(const PurchaseInfo& info, bool success,
                                 const juce::String& statusDescription) override {
        const bool ownsPro = success && info.purchase.productId == kProProductId;
        if (ownsPro) {
            owner.entitlements_.setPro(true);
            owner.persistEntitlement(true);
        }
        if (owner.purchaseDone_) {
            auto done = std::move(owner.purchaseDone_);
            owner.purchaseDone_ = nullptr;
            done(ownsPro, statusDescription);
        }
        if (owner.shell_ != nullptr) owner.shell_->refreshProState();
    }
};

void AndroidAudioApp::initBilling() {
    // Seed from cache so an offline launch keeps Pro unlocked.
    const auto parsed = juce::JSON::parse(entitlementCacheFile().loadFileAsString());
    if (auto* obj = parsed.getDynamicObject())
        entitlements_.setPro(bool(obj->getProperty("pro")));
    billingListener_ = std::make_unique<BillingListener>(*this);
    auto* iap = juce::InAppPurchases::getInstance();
    iap->addListener(billingListener_.get());
    iap->restoreProductsBoughtList(false);   // async ownership query
}

void AndroidAudioApp::purchasePro(std::function<void(bool, juce::String)> done) {
    purchaseDone_ = std::move(done);
    juce::InAppPurchases::getInstance()->purchaseProduct(kProProductId);
}

void AndroidAudioApp::restorePurchases(std::function<void(bool, juce::String)> done) {
    // The listener's purchasesListReceived applies the result; report
    // completion optimistically after it fires via refreshProState. For the
    // explicit button, re-query and answer from the refreshed state.
    juce::InAppPurchases::getInstance()->restoreProductsBoughtList(true);
    juce::MessageManager::callAsync([this, done = std::move(done)] {
        done(entitlements_.isPro(),
             entitlements_.isPro() ? "Pro restored" : "No purchase found yet");
    });
}
```

NOTE for implementer: check JUCE 8.0.15's exact `Listener` virtual names in
`juce_product_unlocking/in_app_purchases/juce_InAppPurchases.h` — if the API
differs (e.g. `productsInfoReturned`, `PurchaseInfo` shape), adapt the
overrides; the semantics above are the contract. `~AndroidAudioApp` must
`removeListener(billingListener_.get())` before destruction.

- [ ] **Step 3: Wire into ctor + shell services**

In `AndroidAudioApp.cpp` ctor, after `shell_->setBrowseServices(...)`:

```cpp
    shell_->setProServices(
        [this] { return entitlements_.isPro(); },
        [this](AppShell::DoneFn done) { purchasePro(std::move(done)); },
        [this](AppShell::DoneFn done) { restorePurchases(std::move(done)); });
    initBilling();
```

(`setProServices` + `refreshProState` are Task 4's AppShell API — implement both tasks against these exact names.)

- [ ] **Step 4: Register TU + build**

Add `Source/app/android/AndroidBilling.cpp` to the Android `target_sources`. Build as in Task 2 Step 2. Expected: BUILD SUCCESSFUL.

- [ ] **Step 5: clang-format + commit**

```bash
clang-format -i Source/app/android/AndroidBilling.cpp Source/app/android/AndroidAudioApp.h Source/app/android/AndroidAudioApp.cpp
git add Source/app/android CMakeLists.txt
git commit -m "feat: Play billing wrapper — pro entitlement query/purchase/restore with offline cache"
```

---

### Task 4: Paywall overlay + AppShell Pro services

**Files:**
- Create: `Source/app/ui/PaywallPanel.cpp` + `Source/app/ui/PaywallPanel.h` (own TU, ≤400 lines)
- Modify: `Source/app/ui/AppShell.h` / `AppShell.cpp` (services + overlay hosting)
- Modify: `CMakeLists.txt` (register TU)

**Interfaces:**
- Consumes: Task 3's service lambdas.
- Produces: `AppShell::setProServices(std::function<bool()> isPro, std::function<void(DoneFn)> purchase, std::function<void(DoneFn)> restore)`; `AppShell::refreshProState()` (re-reads isPro, updates lock glyphs, closes paywall on success); private `void openPaywall(const juce::String& reason)`. `PaywallPanel` is a `juce::Component` with `std::function<void()> onBuy, onRestore, onClose;` and `void setBusy(bool)`.

- [ ] **Step 1: PaywallPanel component**

House style (mirror the stack-picker overlay in `StacksScreen.cpp`): panel `0xf214101f`, rounded 14, `inkA(0.18)` border, height-capped ≤55% of content bounds, scrollable if needed. Content top-to-bottom: drag handle; title `"NAM Player Pro"` (displayFont 21); reason line (uiFont 12, `inkA(0.55)`) — the string passed to `openPaywall`; three ✓ rows (uiFont 12): `"Unlimited rigs & stacks"`, `"List & grid deck layouts"`, `"Every DI audition track"`; price button (accent pill): `"UNLOCK · $9.99"` label placeholder replaced by the store's localized price when product info arrives (JUCE `getProductsInformation({kProProductId})` — wire through a `setPriceText(juce::String)` setter AppShell calls); ghost pill `"RESTORE PURCHASE"`; `"not now"` text row (closes). Hit-testing per press/drag/tap house rules; overlay painted LAST.

- [ ] **Step 2: AppShell hosting**

`setProServices` stores the three lambdas. `openPaywall(reason)`: create-if-needed `paywall_` child, `setBusy(false)`, `toFront`, wire `onBuy → purchase_(done: refreshProState + close-on-success)`, `onRestore → restore_(same)`, `onClose → hide`. `refreshProState()`: `play_->setProState(isPro_())` (Task 5 adds that setter), close paywall if now Pro, `repaint()`.

- [ ] **Step 3: Build + emulator screenshot**

Build; on emulator trigger `openPaywall` temporarily from the ⋯ menu (debug row, removed in Task 5's commit); screenshot to verify house style. Expected: overlay renders, scrolls, dismisses.

- [ ] **Step 4: clang-format + commit**

```bash
clang-format -i Source/app/ui/PaywallPanel.* Source/app/ui/AppShell.*
git add Source/app/ui CMakeLists.txt
git commit -m "feat: paywall overlay + AppShell pro services"
```

---

### Task 5: Gates (internal-testing configuration)

**Files:**
- Modify: `Source/app/ui/AppShell.cpp` (STACKS nav gate, ViewType gate, demo-track gate)
- Modify: `Source/app/ui/PlayScreen.h/.cpp` (`void setProState(bool)`; lock glyphs on gated menu rows)

**Interfaces:**
- Consumes: `isPro_()` via stored service; `openPaywall(reason)` (Task 4).
- Produces: gated behavior per spec table. PlayScreen gains `setProState(bool pro)` → repaints menus; gated ViewType rows 1–3 and demo rows 1+ render a small vector lock (padlock: rounded-rect body + arc shackle, `inkA(0.4)`) when `!pro_`.

- [ ] **Step 1: STACKS nav gate**

In `AppShell::mouseDown` STACKS hit: `if (!isPro_ || isPro_()) { show(Screen::Stacks); } else { openPaywall("Stacks — build and switch full rigs"); }` (null service = ungated, keeps desktop/dev builds working).

- [ ] **Step 2: ViewType + demo gates**

PlayScreen reports selections (existing callbacks); AppShell decides: in the ViewType path, `if (mode != 0 && isPro_ && !isPro_()) { openPaywall("List & grid layouts"); return; }` before applying; demo-track path likewise for `index > 0` with reason `"The full DI track library"`. PlayScreen lock glyphs from `setProState` (glyph only — rows stay visible/tappable per spec).

- [ ] **Step 3: Emulator verification (E2E bar)**

Free state (no license tester yet): STACKS tap → paywall; grid selection → paywall; demo track 2 → paywall; swipe view + demo track 1 work. Screenshots into the commit message record.

- [ ] **Step 4: clang-format + commit**

```bash
clang-format -i Source/app/ui/AppShell.cpp Source/app/ui/PlayScreen.h Source/app/ui/PlayScreen.cpp
git add Source/app/ui
git commit -m "feat: pro gates — stacks nav, layouts, demo tracks (internal-testing config)"
```

---

### Task 6: Release engineering

**Files:**
- Modify: `Builds/Android/app/build.gradle` (release signing config, versionCode/versionName)
- Create: `docs/business/play-release-checklist.md` (Console steps — manual)
- Create: `docs/legal/privacy-policy.md` (content for the hosted page)

**Interfaces:**
- Produces: a signed release `.aab` build command; the checklist Chris executes in Play Console.

- [ ] **Step 1: Keystore + signing**

Generate upload keystore (command documented in the checklist, run by Chris — keystore NEVER committed; path + passwords via `~/.gradle/gradle.properties`):
`keytool -genkeypair -v -keystore ~/keystores/namplayer-upload.jks -keyalg RSA -keysize 2048 -validity 10000 -alias namplayer`
`build.gradle`: `signingConfigs.release` reading `NAMPLAYER_UPLOAD_STORE_FILE/PASSWORD/KEY_ALIAS/KEY_PASSWORD` properties, `buildTypes.release { signingConfig signingConfigs.release }`; `versionCode 1`, `versionName "1.0.0-internal.1"`; confirm release native build stays RelWithDebInfo. Add `.jks`/keystore patterns to `.gitignore`.

- [ ] **Step 2: Privacy policy content**

`docs/legal/privacy-policy.md`: no ads, no analytics, no tracking; network use = TONE3000 login (OAuth; tokens stored only on-device) + tone/artwork downloads; purchases processed by Google Play; contact email. Checklist notes it must be hosted at a public URL before listing submission (GitHub Pages on a separate public repo, or any static host).

- [ ] **Step 3: Console checklist doc**

`docs/business/play-release-checklist.md`, ordered: developer account; merchant profile + banking/tax; app entry (app name decision flagged — see naming risk); data-safety form answers (matching the privacy policy); content rating questionnaire; `pro_unlock` product ($9.99, non-consumable); internal-testing track + tester emails; license testers (test purchases don't charge); upload `.aab` (`./gradlew bundleRelease`).

- [ ] **Step 4: Build the bundle**

Run: `cd Builds/Android && JAVA_HOME=... ./gradlew bundleRelease 2>&1 | grep -E "error|BUILD"`
Expected: BUILD SUCCESSFUL (unsigned OK until Chris creates the keystore; document both states).

- [ ] **Step 5: Commit**

```bash
git add Builds/Android/app/build.gradle .gitignore docs/business/play-release-checklist.md docs/legal/privacy-policy.md
git commit -m "chore: release signing scaffold, versioning, privacy policy, Play checklist"
```

---

### Task 7: Legal/launch blockers (docs + actions for Chris)

**Files:**
- Create: `LICENSE` (proprietary), `docs/business/tone3000-email-draft.md`, `docs/business/nam-naming-note.md`
- Modify: `README.md` (license line)

**Interfaces:** none downstream — these unblock store submission per the assessment's ordered checklist.

- [ ] **Step 1: LICENSE swap**

`LICENSE`: "Copyright © 2026 Chris Harper. All rights reserved." + note that third-party components remain under their own licenses (NeuralAudio MIT, RTNeural BSD, Eigen MPL2, JUCE per its license, dr_wav public domain/MIT) with attribution preserved. README updated: license = proprietary; remove GPLv3 references.

- [ ] **Step 2: TONE3000 email draft**

Partnership-framed: who/what (Android NAM player using their public API, parity rule = everything their site offers free stays free in-app), ask (written OK for a paid app-native tier alongside free catalog access; future iOS redirect URI; optional community mention at launch). Chris sends manually.

- [ ] **Step 3: NAM naming note**

One-pager: the mark situation, option A (courtesy email to Steven Atkinson — draft included), option B (rename + descriptive subtitle "player for Neural Amp Modeler captures"). Decision owner: Chris, deadline: before ANY public impression.

- [ ] **Step 4: Repo private**

After Chris confirms in-session: `gh repo edit cloudcodetree/nam-app --visibility private --accept-visibility-change-consequences`. Record in decisions.md.

- [ ] **Step 5: Commit**

```bash
git add LICENSE README.md docs/business
git commit -m "chore: proprietary license, TONE3000 + naming drafts (launch blockers)"
```

---

### Task 8: First-rig-free soft paywall (public-launch swap — build now, flag-flip later)

**Files:**
- Modify: `Source/app/ui/AppShell.cpp` (stacks callbacks: gate CREATION of rig #2 instead of the nav; nav gate removed behind a constant)
- Test: covered by Task 1's `canSaveRig` tests (policy) + emulator E2E

**Interfaces:**
- Consumes: `Entitlements::canSaveRig(int)` via `isPro_` service + stack count; `openPaywall`.

- [ ] **Step 1: Gate the second rig**

In the `stacks_->onCreate` wiring: `const int existing = (int) stackList_.size(); if (isPro_ && !isPro_() && existing >= nam::Entitlements::kFreeRigs) { openPaywall("Your first rig stays free forever — Pro adds unlimited rigs"); return; }` then existing create path. A compile-time `constexpr bool kSoftPaywall` selects nav-gate (internal) vs rig-gate (public); default internal now, flipped in the public-launch commit.

- [ ] **Step 2: E2E on emulator**

Free: create rig 1 (works, fully editable), tap + NEW STACK again → paywall with the first-rig copy. Screenshot.

- [ ] **Step 3: clang-format + commit**

```bash
clang-format -i Source/app/ui/AppShell.cpp
git add Source/app/ui/AppShell.cpp
git commit -m "feat: first-rig-free soft paywall behind public-launch flag"
```

---

## Out of scope (separate plans)

- MIDI foot control (approved design; own spec+plan next — lands as a Pro feature).
- iOS port; Looper; Cloud Sync; public-launch marketing assets.

## Self-review notes

Spec coverage: product definition (T1/T5/T8), architecture (T1/T3/T4), paywall UX (T4), billing spike gate (T2 blocks T3), gating table (T5 internal + T8 public), release engineering (T6), legal (T7), error handling (T3 cache semantics; T4 busy state; store-unavailable path = purchase callback failure surfaces in panel). Types checked: `setProServices/refreshProState/openPaywall/setProState/canSaveRig` used consistently across T3–T8.
