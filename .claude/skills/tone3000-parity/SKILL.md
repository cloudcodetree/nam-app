---
name: tone3000-parity
description: Audit tone3000.com's UI and API surface against NAM Player for functional parity, and flag any app paywall that gates TONE3000-equivalent functionality (violates the CLAUDE.md "TONE3000 parity is free" rule). Run before releases and whenever monetization or browse features change.
---

# TONE3000 functional-parity audit

Goal: a parity report answering two questions —
1. **Coverage**: what does tone3000.com offer that the app doesn't (yet)?
2. **Rule compliance**: does anything behind the app's Pro paywall exist as
   free functionality on tone3000.com? (Any hit = a violation of the
   CLAUDE.md Product rule and must be reported as a BLOCKER-style finding.)

## 1. Scrape the live site surface

Use browser tooling (chrome-devtools MCP if connected; else WebFetch /
firecrawl) against these pages, logged OUT first, then note what changes
when logged IN:

- `https://www.tone3000.com/search` — capture: filter groups and their full
  vocab (gear types, tags, makes/models, technical/architecture, sort
  orders, format toggle), pagination behavior, results-card contents
  (preview player? download button? favorite?).
- A tone detail page (e.g. any result) — capture: audio preview player,
  variant/model list, download affordances, pairing suggestions (amp for a
  cab page etc.), creator info, related tones.
- Account surface (if logged out, infer from nav/marketing): favorites/
  collections, upload, profile.
- Watch the network panel on /search for API calls: confirm the
  `/api/v1/tones/search` params against `Source/net/Tone3000Api.h`
  (`SearchParams`) — new params or vocab values are coverage gaps.

## 2. Diff against the app

App-side sources of truth:
- `Source/net/Tone3000Api.h` (`SearchParams`) + `kGearVocab`/`kTagVocab`/
  `kMakeVocab`/`kSortVocab` in `Source/app/ui/AppShell.cpp` — filter parity.
- `docs/wiki/tone3000.md` + `docs/wiki/ui-system.md` — current feature map.
- The Pro gate list: `nam::Entitlements` (Source/model) and the gating
  touchpoints in `AppShell` — this is the list to check against the rule.

## 3. Report format

```
## Parity report — <date>
### Rule violations (paywall gates site-free functionality)
- [VIOLATION] <app Pro feature> — free on site at <url/evidence>   (or "none")
### Coverage gaps (site has it, app doesn't)
- <feature> — <where on site> — <suggested app home>
### Vocab drift (filters/params)
- <param>: site says <...>, app says <...>
```

## 4. Aftermath

- Violations: fix the gate (make it free) or escalate to Chris — never ship
  a violating build. Update `nam::Entitlements` + tests in the same change.
- Vocab drift: update the AppShell vocab tables + `docs/wiki/tone3000.md`.
- Record a dated line in `docs/wiki/decisions.md` when the audit changes
  anything. New site features worth building go to the coverage-gap list in
  the wiki, not silently dropped.

## Notes

- The site evolves; scrape fresh every run — do not trust the wiki's cached
  vocab as the site's current truth.
- Never log in with credentials in headless runs; logged-out surface is the
  floor for "free on site". If logged-in-only features matter (favorites),
  verify via public docs/marketing pages or ask Chris to check manually.
- API probing stays read-only GETs against documented endpoints — no
  scraping past auth walls, no load generation.
