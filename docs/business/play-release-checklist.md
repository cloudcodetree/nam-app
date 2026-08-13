# Play Console release checklist

Manual steps Chris runs in Google Play Console + local machine. Ordered —
each step assumes the ones above it are done. Nothing here is automated;
this is the runbook.

## 0. Repo visibility (prerequisite, not performed by this checklist)

The source repo (`cloudcodetree/nam-app`) is currently **PUBLIC** on GitHub.
Going private is a separate task (Task 7 of the freemium-Pro-unlock plan)
and must land **before the first store build/upload**, not after. Do not
upload an `.aab` to any Play Console track — including internal testing —
until Task 7 is done, since the artifact and its Play Console listing can
outlive a later repo-visibility change.

## 1. Upload keystore (local, one-time)

Generate the upload keystore — **run by Chris, never by an agent, never
committed**:

```
keytool -genkeypair -v -keystore ~/keystores/namplayer-upload.jks \
  -keyalg RSA -keysize 2048 -validity 10000 -alias namplayer
```

Add the resulting path + passwords to `~/.gradle/gradle.properties`
(outside the repo, never checked in):

```
NAMPLAYER_UPLOAD_STORE_FILE=/Users/chris.harper/keystores/namplayer-upload.jks
NAMPLAYER_UPLOAD_STORE_PASSWORD=...
NAMPLAYER_UPLOAD_KEY_ALIAS=namplayer
NAMPLAYER_UPLOAD_KEY_PASSWORD=...
```

`Builds/Android/app/build.gradle`'s `signingConfigs.release` reads these
four properties and is a no-op (unsigned release build) until all four are
present — safe on any machine that hasn't generated the keystore yet.
`.gitignore` blocks `*.jks`/`*.keystore` repo-wide as a backstop.

Back up the keystore file + passwords somewhere durable outside the repo
(password manager + an offline copy). **Losing the upload key means you can
never update the app under the same listing again.**

## 2. Google Play Developer account

- Enroll at https://play.google.com/console/ (one-time $25 fee).
- Complete identity verification (can take up to 48h).

## 3. Merchant profile + banking/tax

Required before any paid product (`pro_unlock`) can go live:

- Play Console → Setup → Payments profile → create merchant account.
- Banking details for payouts, tax forms (W-9 or local equivalent).
- This can run in parallel with app setup but blocks the IAP product from
  being purchasable — internal testers can still use **license testing**
  (see step 8) before this is done.

## 4. App entry

- Play Console → Create app.
- **App name — UNRESOLVED, Chris decides before any public impression.**
  "NAM Player" trades on Neural Amp Modeler's "NAM" branding; see
  `docs/business/2026-08-13-launch-model-assessment.md` (legal checklist
  item 6, top risk #2): either a courtesy email to Steven Atkinson clearing
  the name, or a rebrand with a descriptive subtitle before this field is
  set for anything beyond internal testing. **Internal testing is fine
  under any working name** — this is a public-listing-time decision, not a
  build-time one.
- Category: Music & Audio. Free app (contains one paid IAP item).
- Default language, declarations (ads: no, target audience, etc. — see data
  safety in step 6).

## 5. Store listing assets

- Short/full description, screenshots (phone), feature graphic, icon.
- Not scaffolded here — content/creative pass, separate from this
  engineering checklist.

## 6. Data safety form

Play Console → App content → Data safety. Answers must match
`docs/legal/privacy-policy.md` exactly:

- Data collected: **none** shared with third parties for their purposes.
- Data types touched: none of the standard categories (no location,
  contacts, personal identifiers) are collected by the app itself.
- Network use disclosed: TONE3000 OAuth sign-in (third-party auth), tone/
  artwork downloads from TONE3000's CDN, purchase processing via Google
  Play Billing.
- Security practices: data (OAuth tokens) encrypted in transit (HTTPS);
  no data transmitted to NAM Player's own servers (there are none) — tokens
  live only in on-device app-private storage.
- Data deletion: uninstalling the app deletes all locally stored data
  (tokens, cached tones/artwork); there's no account with us to delete.

## 7. Content rating questionnaire

- Play Console → App content → Content rating.
- Music/audio processing tool, no user-generated content shared publicly,
  no violence/gambling/etc. — expect the lowest rating tier (e.g. "Everyone" /
  IARC 3+). Answer the questionnaire honestly; do not pre-guess beyond that.

## 8. In-app product: `pro_unlock`

Play Console → Monetize → Products → In-app products → Create product.

- Product ID: `pro_unlock` (must match `kProProductId` in
  `Source/app/android/AndroidBilling.cpp` exactly — it's a literal string,
  not looked up from config).
- Type: **Managed product (non-consumable)** — one-time purchase, restored
  via `restoreProductsBoughtList`, never re-charged.
- Price: **$9.99 USD** (Play auto-converts other currencies/tiers).
- Status: Active.
- Requires the merchant profile (step 3) to be complete before it can go
  Active/purchasable; can be created in Draft earlier.

## 9. Internal testing track

Play Console → Testing → Internal testing → Create release.

- Add tester emails (Chris + anyone else helping verify) as a list or
  Google Group.
- Internal testing has no review wait and is where the "NAM" naming
  question (step 4) does NOT need to be resolved — safe to iterate here.
- Publish the release with the tester list before uploading the first
  build so testers get the opt-in link ready to go.

## 10. License testers (test purchases that don't charge)

Play Console → Setup → License testing (or Monetize setup, depending on
console version) → add the same tester Google accounts.

- License testers see real Play Billing UI but purchases are simulated —
  no card charged, and purchase state (`pro_unlock` owned) behaves exactly
  like a real purchase for testing the entitlement/paywall flow end to end.
- Testers must be signed into the Play Store with the same account added
  here AND be on the internal-testing tester list (step 9) to install the
  build at all.

## 11. Build and upload the `.aab`

```
cd Builds/Android
JAVA_HOME=/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home \
  ./gradlew bundleRelease
```

- Output: `Builds/Android/app/build/outputs/bundle/release/app-release.aab`.
- **Signed** once step 1's `~/.gradle/gradle.properties` keys are present;
  **unsigned** (BUILD SUCCESSFUL, but Play Console will reject an unsigned
  upload) if they're absent — see `docs/wiki/build-deploy.md` for both
  observed states. Play Console requires a signed bundle; do the upload
  from a machine with the keystore configured.
- Upload the `.aab` to the internal testing release created in step 9.

## 12. Roll out and verify

- Roll out the internal-testing release.
- Install via the opt-in link on a tester device, sign in to TONE3000,
  exercise a license-tester purchase of `pro_unlock`, confirm the paywall
  and Pro gates unlock as expected, confirm a real-device audio path still
  meets latency expectations (RelWithDebInfo native build — see
  `docs/wiki/build-deploy.md`).

## Deferred to later (not blocking internal testing)

- Repo private (step 0) — blocks any upload, not internal-testing setup
  itself, but must land first regardless.
- Public listing / production track — blocked on the "NAM" naming decision
  (step 4), TONE3000 written partnership blessing, and store listing
  creative (step 5).
