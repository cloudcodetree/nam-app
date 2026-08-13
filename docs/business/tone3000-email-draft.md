# TONE3000 partnership email — draft

Status: DRAFT — Chris sends manually, not automated. Nothing here has been
sent as of this writing.

Context for whoever reviews this before sending: the app currently has
Play internal testing as its only live channel; production is gated on a
reply to this email (see `docs/business/2026-08-13-launch-model-assessment.md`,
legal checklist item 4). Send early — the assessment flags this as a
multi-week-lag item, so it should go out during the internal-testing phase,
not after.

## Recipient

TONE3000 (via their listed developer/partnerships contact — check
tone3000.com for the current address at send time).

## Subject

NAM Player (Android) — built on the TONE3000 API, a couple of quick asks

## Body

Hi TONE3000 team,

I'm Chris, a solo developer building **NAM Player**, an Android app (iOS
planned later) for playing Neural Amp Modeler captures. It's built on your
public API — OAuth2 PKCE as a public client, using only a publishable key
(no client secret embedded or logged) — so users can browse, preview, and
download tones from your catalog straight into the app.

The app follows a parity rule I've committed to in the project's own
engineering standards: anything TONE3000.com offers users for free —
search, filters, tone pages, audio previews, downloads, favorites/
collections — stays free in the app, no exceptions. The paid tier (a single
one-time $9.99 unlock) only gates app-native additions that aren't part of
your product at all: rig-building/Stacks, alternate view layouts, extra DI
audition tracks, and future MIDI/looper features.

A few things I'd appreciate from you before I move past internal testing:

1. **Written OK** to build a commercial (paid-tier) app on top of the
   public API under the terms above — free full access to everything your
   site offers for free, monetization limited to app-native additions.
2. **A redirect URI for a future iOS build** — I'll need one registered
   alongside the existing Android PKCE redirect once iOS work starts.
3. **Optional** — if you'd like a mention to your community around launch
   (release notes, social, etc.), happy to coordinate on timing and
   wording.

Right now the app is in Play internal testing only; I'm holding production
until I hear from you. Happy to share a build, screenshots, or answer any
questions about the integration.

Thanks for considering it, and for the API in the first place.

Chris Harper

## Notes for future edits

- No user counts, revenue figures, or partnership status are claimed above
  — none exist yet to claim. Update this draft with real numbers only once
  they're real.
- If TONE3000 responds with conditions, log the outcome in
  `docs/wiki/decisions.md` and update
  `docs/business/2026-08-13-launch-model-assessment.md` checklist item 4.
