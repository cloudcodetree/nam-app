# NAM Player — Privacy Policy

_Last updated: 2026-08-13_

This document is the content for NAM Player's privacy policy. **It must be
hosted at a public URL before the app can be submitted for any Play Console
listing (internal testing is fine without one; closed/open testing and
production require the URL in Play Console's app-content section).** Publish
it via GitHub Pages on a separate public repo (the app's source repo,
`cloudcodetree/nam-app`, is going private before the first store build — see
`docs/business/play-release-checklist.md`) or any static host, then paste the
resulting URL into Play Console → App content → Privacy policy.

## Summary

NAM Player does not run ads, does not use analytics or crash-telemetry SDKs,
and does not track you. The app talks to the network only for the things
listed below, and only because you asked it to.

## What the app does NOT do

- No advertising, ad networks, or ad SDKs.
- No analytics, telemetry, or crash-reporting SDKs.
- No tracking, fingerprinting, or behavioral profiling.
- No sale or sharing of personal data with third parties for their own
  purposes.
- No account system of our own — there is nothing for us to host or breach.

## What the app DOES send or receive over the network

1. **TONE3000 sign-in (OAuth / PKCE).** To browse, preview, and download
   amp/cab tones from [tone3000.com](https://www.tone3000.com), the app
   authenticates you against TONE3000's own OAuth service. The app is a
   public PKCE client — it never holds or transmits a client secret, only a
   publishable client identifier. TONE3000 issues an access token and a
   refresh token; both are written to a single JSON file in the app's
   private storage on your device (0600 file permissions, app-sandboxed,
   never readable by other apps) and are never sent anywhere except back to
   TONE3000's API to authorize your requests. Deleting the app deletes the
   stored tokens. TONE3000's own privacy policy governs what TONE3000 does
   with your account data once you're signed in there.
2. **Tone and artwork downloads.** Amp/cab model files (`.nam`, impulse
   responses) and pack artwork you choose to download are fetched directly
   from TONE3000's CDN and cached in the app's private storage on-device.
3. **In-app purchase.** The one-time "Pro unlock" purchase is processed
   entirely by Google Play Billing. NAM Player never sees your payment
   details — Google handles the transaction and reports back only whether
   you own the `pro_unlock` product. See Google Play's privacy policy for
   how Google handles payment data.

## Data NOT collected by NAM Player

We do not collect your name, email, physical address, precise or coarse
location, contacts, photos (beyond what you explicitly export), or device
identifiers for advertising. The only identity-adjacent data on your device
is the TONE3000 OAuth token pair described above, which lives in app-private
storage and is never transmitted to us — NAM Player has no backend server.

## Permissions

The app requests microphone access (`RECORD_AUDIO`) to process your guitar
signal in real time through the amp models you select. Audio is processed
entirely on-device and is never recorded, stored, or transmitted anywhere.

## Children's privacy

NAM Player is not directed at children and does not knowingly collect data
from children.

## Changes to this policy

If the app's data practices change (for example, adding optional analytics
in a future version), this page will be updated and the "Last updated" date
above will change accordingly. Material changes will be called out in the
Play Store release notes.

## Contact

Questions about this policy or the app's data handling: **aphid310@gmail.com**
