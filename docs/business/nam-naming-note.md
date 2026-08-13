# "NAM" naming risk — decision note

Status: OPEN — decision owner **Chris**. Deadline: **before any public
impression** (any listing, screenshot, or announcement a stranger could
see). Internal testing (Play internal track, direct-install testers) is
fine under either option below — this only has to be resolved before the
first *public* one.

## The situation

"NAM" / "Neural Amp Modeler" is Steven Atkinson's open-source project
(neuralampmodeler.com, github.com/sdatkinson/neural-amp-modeler). This app
is currently built and referred to internally under the working name
**"NAM Player"**. Shipping a store listing under that name risks
presenting the app as Steven's project, or as officially affiliated with
it, when it is neither — it's an independent player app that happens to
run NAM-format captures (and TONE3000's catalog of them). That's a
trademark/goodwill risk to Steven's project and a listing-takedown risk to
us, independent of the TONE3000 relationship (see
`docs/business/tone3000-email-draft.md`, which is a separate ask to a
separate party).

## Option A — courtesy email to Steven Atkinson

Ask for his blessing (or at least give notice) before using "NAM" in a
store listing. Cheap, low-friction, and the more collegial path given this
app exists because his open-source project exists. Draft below.

### Draft email

> Subject: Android app playing NAM captures — quick heads-up before I list it
>
> Hi Steven,
>
> I'm Chris, a solo developer. I've built an Android app (iOS planned
> later) that plays `.nam` captures on-device — a "player" app, not a
> capture/training tool — and lets users pull tones from TONE3000's
> catalog into it. It's currently called "NAM Player" internally and I'm
> getting close to a public store listing.
>
> Before I do, I wanted to check in: are you OK with an app using "NAM" in
> its name/listing this way, or would you rather I use a descriptive
> name instead (e.g. "player for Neural Amp Modeler captures") and credit
> the project without using the name/mark directly? Either is fine by me —
> just want to do right by the project this is all built on.
>
> Happy to share more about the app if useful.
>
> Thanks for NAM in the first place — this app wouldn't exist without it.
>
> Chris

If he's fine with it (or doesn't respond in reasonable time and no
conflicting signal turns up), Option A lets the app keep the "NAM Player"
name.

## Option B — rename + descriptive subtitle

Rename the app and use a descriptive subtitle instead of the mark itself:
subtitle **"player for Neural Amp Modeler captures"** — this is nominative
fair use (referring to NAM to describe compatibility, not claiming to be
NAM or using its mark as the product's own name). Lower risk, no
dependency on a reply from a third party, but requires a rename pass
(store listing name, in-app branding, package/app IDs if not already
locked by an internal-testing release).

## Recommendation

No recommendation forced here — this is Chris's call. Option A is
cheaper/faster if Steven responds positively; Option B removes the
dependency entirely and is the safer default if a reply doesn't come back
before the deadline. Either is compatible with continuing internal
testing under the current name in the meantime.

## What happens next

- Chris picks A or B (or "send A, fall back to B if no reply by [date]").
- Log the decision and date in `docs/wiki/decisions.md`.
- If B: also update `README.md`, the Play Console listing name, and
  in-app branding strings in the same change.
