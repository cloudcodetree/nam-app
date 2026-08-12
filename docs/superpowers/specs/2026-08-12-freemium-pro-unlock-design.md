# Freemium "Pro" unlock — design

**Date:** 2026-08-12 · **Status:** approved (Chris, 2026-08-12); revised
same day for the TONE3000-parity rule (see below).
**Scope:** Android first (internal testing track). iOS port and public-launch
polish are separate future specs.

## Governing rule (added 2026-08-12, CLAUDE.md "Product rules")

**Anything tone3000.com offers free is free in the app.** The site gives
away search/filters, tone pages, audio previews, unlimited downloads, and
favorites — so none of that may sit behind Pro. The paywall gates only
APP-NATIVE features. This removed the originally-planned 10-save cap.
The `/tone3000-parity` skill audits the boundary against the live site.

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

**Free forever** (everything TONE3000-equivalent, plus the playing core)
- Live playing: full engine chain (model, cab/IR, gate, EQ, delay, reverb),
  quick settings, orb/engine/mute controls, tuner.
- Browse + search + filters over the entire TONE3000 catalog (infinite
  scroll) — site parity.
- **Unlimited downloads, saves, and favorites** — site parity (the original
  10-save cap is removed by the governing rule).
- Base tone audition (hearing a tone before downloading is site parity;
  the default DI track is free).
- Swipe-card deck view.

**Pro ($9.99 one-time)** — app-native only
- **Stacks** (create/apply rigs — no site equivalent).
- Detail-list and grid **view layouts** (app UI; swipe cards remain free).
- The **extended DI audition track library** (auditioning through a CHOICE
  of playing styles is an app enhancement; the site's previews are fixed
  recordings — first track free, the rest Pro).
- First claim on future app-native features (MIDI, looper, pedalboard).

**Grandfathering:** no longer needed — nothing caps saves.

## Architecture

### nam::Entitlements (JUCE-free, Source/model, TDD)

Pure policy class — no billing, no JUCE:

```
class Entitlements {
    void setPro(bool);            bool isPro() const;
    // TONE3000-parity rule: saves/downloads/favorites are ALWAYS allowed.
    bool canUseStacks() const;                    // pro
    bool canUseLayout(int layoutMode) const;      // pro || layoutMode == 0
    bool canUseDemoTrack(int index) const;        // pro || index == 0
    static constexpr int kFreeDemoTracks = 1;
};
```

Headless tests land in the same commit (house TDD rule): pro bypass,
layout gates, demo-track gate, and a regression pin: no API exists for
capping saves (the parity rule made that a non-feature).

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
| Stacks | STACKS nav hit in AppShell `mouseDown` | paywall instead of `show(Stacks)` |
| List/grid layouts | ViewType menu selection (PlayScreen reports; AppShell decides) | lock glyph on rows 1–3; selection opens paywall |
| Demo tracks 2+ | Demo-track menu selection | lock glyph; selection opens paywall |

Saves, downloads, hearts, browse, filters, and base audition have **no
gate** (TONE3000-parity rule).

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
  saved tones are untouched (saves are never gated) — only Stacks/layouts/
  extra demo tracks re-lock.

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
