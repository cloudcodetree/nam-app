# Launch business-model assessment (business-advisor, 2026-08-13)

Status: DELIVERED — adoption pending Chris's decision.
Inputs: freemium spec (2026-08-12), Claude Design "NAM Add-ons Hi-Fi"
exploration, wiki decisions/controllers, CLAUDE.md parity rule.

## Bottom line

Ship the committed spec's **single $9.99 Pro unlock**, not à-la-carte.
Steal exactly one idea from the design exploration — **first-rig-free with
the paywall on saving rig #2** (public launch; hard gate is fine for
internal testing) — and cut the rest for launch: tier-picker onboarding,
Performer bundle, Looper SKU (unbuilt), Cloud Sync sub (no backend),
7-day trial (free tier already IS the trial; app-side trials on one-time
IAPs are self-inflicted abuse surface with no backend to anchor them).

## Key verdicts

- **À-la-carte becomes legitimate only when a second real product ships**
  (Looper as its own $4.99–$7.99 one-time add-on later — that's the moment
  a storefront exists; today it would be 1 real SKU among placeholders).
- **Stacks $9.99: correct.** Anchors beautifully vs ToneX ($19.99/collection
  or $9.99/MONTH) and BIAS FX 2 Mobile (~$70–100 unlock). One-time $9.99 vs
  ToneX's $9.99/month is the comparison-table win.
- **Bundle: killed.** 17% discount persuades no one, and "all future v1
  add-ons" sells the roadmap for $5. If ever revived: named, shipped
  products only.
- **"Own v1": keep as internal economics** (never re-charge for versions;
  charge for new MODULES instead). Never print "v1" in the store.
- **$2.99 micro-SKU**: one price-ladder satellite later (app-native only,
  e.g. DI-track pack); not at launch.
- **MIDI foot control = Pro** (per controllers.md), NOT free — the design
  board's "one rig with your MIDI controller free" pre-spends the strongest
  future Pro upgrade. Conflict resolved in favor of controllers.md.
- **Fees verified**: ~15% both stores under $1M (Apple Small Business
  Program — enroll; Google sub-$1M tier; Google's 2026-06 restructure ≈
  same effective rate). Net ≈ **$8.49 per $9.99 sale**. Stores are merchant
  of record (tax is their problem).

## Year-1 revenue (Android only, $0 marketing, assumptions shown)

| Scenario | Installs | Conv. | Net revenue |
|---|---|---|---|
| Low  | 2,000  | 1.5% | ~$255 |
| Mid  | 8,000  | 3%   | ~$2,040 |
| High | 25,000 | 5%   | ~$10,600 |

Fixed burn year 1: ~$25–150 (Play $25, domain; Apple $99/yr only at iOS;
JUCE Personal $0 until $50k → Indie $480/yr). Honest read: year-1 is gear
money; the order-of-magnitude levers are the **iOS port** (where amp-sim
users pay) and **MIDI-as-Pro** (highest-intent segment). Structural gift:
ToneX/BIAS are iOS-only — Android's NAM shelf is empty.

## 90-day marketing sketch

- 0–30 (internal): billing spike; legal items 1–3; TONE3000 email as
  PARTNERSHIP opener (blessing + ask for community mention); 60-second
  latency-proof demo video.
- 30–60 (closed beta): 50–200 testers from r/NeuralAmpModeler, TONE3000
  community, TheGearPage; fix top-3 device latency complaints pre-public.
- 60–90 (public): same-week Reddit + TONE3000 announcement + 5–10 small
  (5k–50k sub) NAM YouTube channels; ASO on "neural amp modeler / NAM /
  amp sim android"; listing states the USB-interface requirement UP FRONT.

## Legal checklist (by blocking-ness)

1. Repo private + proprietary LICENSE (blocks any store build).
2. Privacy policy URL + Play data-safety form (blocks the listing).
3. Play Console merchant/banking/tax + content rating + encryption
   boilerplate (blocks selling).
4. TONE3000 written blessing (blocks PRODUCTION; send now — weeks of lag).
5. JUCE tier tripwire at $50k → Indie (calendar it).
6. **"NAM" trademark care — decide BEFORE public listing**: courtesy email
   to Steven Atkinson for the name, or rebrand with descriptive subtitle.
   Cheap now, expensive later.
7. EULA: store defaults suffice at this scale.

## Top 5 risks

1. **TONE3000 dependency is existential** → written agreement, offline
   grace (saved tones work forever), local .nam import.
2. **"NAM" name** → resolve during internal testing, before any public
   impression.
3. **Android-first is the small half of paying users** → hold expectations;
   iOS port is the revenue launch.
4. **Parity rule caps conversion by design** → paid value rests on Stacks
   being GREAT; first-rig-free converts on experienced value; MIDI-as-Pro
   next.
5. **Solo-dev ops surface** → staged rollout %, explicit device
   expectations in listing, reserved support time first 30 public days.

Full sourced report in the session archive; sources include Apple SBP,
Google Play fee docs (incl. 2026-06 restructure), IK ToneX pricing,
Engadget BIAS FX 2 pricing, RevenueCat fee guide.
