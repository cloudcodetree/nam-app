# Freemium "Pro" unlock — design

**Date:** 2026-08-12 · **Status:** approved (Chris, 2026-08-12)
**Scope:** Android first (internal testing track). iOS port and public-launch
polish are separate future specs.

## Decision summary

| Decision | Choice |
|---|---|
| Monetization | Free app + one-time **Pro** in-app purchase (no ads, no subscription) |
| Free/Pro split | Player free, rig-builder Pro (§Product definition) |
| Price | **$9.99** one-time, non-consumable product `pro_unlock` |
| Billing | JUCE `juce_product_unlocking` (Play Billing now, StoreKit later on iOS) |
| JUCE license | Personal tier (splash stays ON), upgrade to Indie when revenue warrants |
| Repo | Goes **private** before the first store build; proprietary license replaces the GPLv3 plan |
| Launch bar | Play **internal testing** track only; production later |

## Product definition

**Free forever**
- Live playing: full engine chain (model, cab/IR, gate, EQ, delay, reverb),
  quick settings, orb/engine/mute controls, tuner.
- Browse + audition the entire TONE3000 catalog (infinite scroll, filters).
- Up to **10 saved tones on device** (hearts + downloads combined — one
  library, so the cap counts library entries of both types).
- Swipe-card deck view; first **3 demo tracks**.

**Pro ($9.99 one-time)**
- Unlimited saves/favorites.
- **Stacks** (create/apply rigs).
- Detail-list and grid **view layouts** (swipe cards remain free).
- Full demo-track library.

**Grandfathering:** an install already holding >10 library entries keeps them
all; the cap only blocks NEW saves while free. Un-saving frees slots.

## Architecture

### nam::Entitlements (JUCE-free, Source/model, TDD)

Pure policy class — no billing, no JUCE:

```
class Entitlements {
    void setPro(bool);            bool isPro() const;
    // Save-cap policy: existing = current library count.
    bool canSave(int existingEntries) const;      // pro || existing < kFreeSaveCap
    bool canUseStacks() const;                    // pro
    bool canUseLayout(int layoutMode) const;      // pro || layoutMode == 0
    bool canUseDemoTrack(int index) const;        // pro || index < kFreeDemoTracks
    static constexpr int kFreeSaveCap = 10;
    static constexpr int kFreeDemoTracks = 3;
};
```

Headless tests land in the same commit (house TDD rule): cap boundary,
grandfather semantics (existing > cap ⇒ existing entries untouched, canSave
false), pro bypass, layout/demo gates.

### Host billing wrapper (AndroidToneServices or new AndroidBilling.cpp TU)

- Startup: async `InAppPurchases::getProductsInformation` /
  `queryEntitlements`; on result, `entitlements_.setPro(owned)` and persist
  the last-known state to `appdata/NAM Player/entitlement.json`
  (existence/state only — no receipts logged).
- Offline launch: seed from the cached state. **Cache is a fallback, never a
  grant** — a Play query saying "not owned" overwrites it.
- Services injected into AppShell (same pattern as BrowseServices):
  `isPro()`, `purchasePro(done)`, `restorePurchases(done)`.
- Purchase/restore callbacks re-validate on the message thread and refresh
  the UI (paywall closes on success; gated controls unlock live).

### Paywall UI (Source/app/ui, own TU ≤400 lines)

House-style overlay: height-capped, scrollable, painted last. Content:
feature list (vector icons), "$9.99 · one time", BUY, RESTORE, close.
Opens ONLY on touching a gated feature — never on launch. Gated menu rows
show a lock glyph instead of hiding (discoverability).

### Gating points (exact touchpoints)

| Feature | Touchpoint | Behavior when free |
|---|---|---|
| 11th save | `onKeepToggle`/`onSaveToggle` (browse mode) before `svc_.keep/save` | paywall opens; no download starts |
| Stacks | STACKS nav hit in AppShell `mouseDown` | paywall instead of `show(Stacks)` |
| List/grid layouts | ViewType menu selection (PlayScreen reports; AppShell decides) | lock glyph on rows 1–3; selection opens paywall |
| Demo tracks 4+ | Demo-track menu selection | lock glyph; selection opens paywall |

Gating decisions live in AppShell (which owns services); PlayScreen stays
presentation-only (receives `setProState(bool)` for lock glyphs).

## Billing spike (first implementation task — go/no-go)

Verify `juce_product_unlocking` in JUCE 8.0.15 bundles a Play Billing
Library version ≥ Google's current Play minimum, and that a test purchase
round-trips on the internal track. If the module is too old and not
trivially patchable, fall back to a hand-rolled Play Billing + JNI bridge
(design addendum required). Nothing else builds on billing until the spike
passes.

## Release engineering

- Release keystore (upload key), signing config in `Builds/Android`,
  `versionCode` (monotonic int) + `versionName` (semver) scheme.
- Release build = RelWithDebInfo native (house rule), minify off initially.
- JUCE splash remains enabled (Personal-tier compliance).
- Play Console: app entry, `pro_unlock` product, internal testing track,
  license-tester accounts (test purchases, no charges).
- Privacy policy page (static): no ads, no analytics, no data sale; TONE3000
  OAuth login + downloads are the only network use. Data-safety form to match.

## Legal / housekeeping

1. GitHub repo → **private** before any store build ships.
2. LICENSE: proprietary "all rights reserved" replaces the GPLv3 plan
   (sole-author relicense; MIT/BSD/MPL deps are compatible).
3. TONE3000 email: commercial-use blessing + heads-up on a future iOS
   redirect URI. Send early; don't block internal testing on the reply, but
   block PRODUCTION on it.
4. Wiki: decision entries + a new `monetization.md` spoke when built.

## Error handling

- Billing unavailable (no Play, network down): paywall shows a "store
  unavailable, try later" row; gates stay closed; cached Pro still honored.
- Purchase interrupted/cancelled: paywall stays open, no state change.
- Refund/revoke: next successful Play query clears `isPro` and the cache;
  saved tones above the cap grandfather exactly like an old install.

## Testing

- `nam::Entitlements`: full headless coverage (TDD, tests target).
- Billing glue: exercised on-device via Play internal track + license
  testers (purchase, cancel, restore, offline launch, airplane-mode grant
  check). The JUCE UI layer has no unit harness — device verification is
  the E2E bar (house rule), recorded in commit messages.

## Out of scope

iOS port (own spec: host layer, StoreKit product, ASWebAuthenticationSession
OAuth), production-launch polish (listing assets, onboarding), sales/promo
pricing, subscriptions.
