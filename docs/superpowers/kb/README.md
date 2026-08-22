# Competitor documentation knowledge base

Manuals, firmware changelogs and official guides for the modelers we compete
with. Collected 2026-08-22. **437,000 words of searchable text.**

Purpose: so a design or architecture question gets answered from the vendor's
own manual rather than from memory.

## Who uses what

The corpus is for **Claude Code**, which can grep. It is NOT for Claude Design.

| Tool | Gets | Why |
|---|---|---|
| Claude Code | this corpus, `text/`, 437k words | it can search; it needs the exact manual paragraph |
| Claude Design | `design-doctrine.md` + 22 curated UI screenshots, pushed into the Claude Design project | it cannot grep, and 437k words would drown it; it needs distilled rules and visual precedent |

`design-doctrine.md` (also at `../designs/design-doctrine.md`) is the distillation
— testable rules plus the antipattern checklist. When something here changes the
doctrine, update BOTH: the doctrine file in the repo and the copy in the Claude
Design project.

## How to use it

Everything is extracted to plain text in `text/`. Grep it:

```bash
cd docs/superpowers/kb/text

# what does a snapshot forbid?
grep -ril "snapshot" . | head

# how does a vendor scope momentary vs latching?
grep -ih -B2 -A2 "momentary" line-6*.txt

# search across line wraps (PDF text wraps mid-sentence -- normalise first)
python3 -c "import glob,re;[print(f,m.group(0)[:200]) for f in glob.glob('*.txt') for m in re.finditer(r'[^.]{0,150}YOUR_TERM[^.]{0,150}\\.', re.sub(r'\\s+',' ',open(f,errors='ignore').read()), re.I)]" 
```

**PDF text wraps mid-sentence.** A plain `grep` for a phrase will miss it if it
crosses a line break. Normalise whitespace first (the python one-liner above)
when searching for anything longer than a word or two.

## Why firmware changelogs matter most

24 of the 60 documents are release notes. They are the highest-signal source in
the set because they record what shipped BROKEN and how the vendor fixed it --
the failures never appear in a manual. Example found in this corpus: Neural DSP
shipped a *Hold Timing* menu in CorOS 4.0.0 making footswitch hold duration
user-configurable (500ms-1s), which is a direct admission that a hardcoded
gesture timing was wrong.

## Contents


### Neural DSP Quad Cortex (CorOS)

- `neural-dsp-quad-cortex-00-manual.txt` — **Quad Cortex User Manual 4.0.0 (CorOS 4.0.0)** (15,889 words, html)  
  The current, authoritative manual and the single richest design document in the set: ~95k characters of full text rendered server-side (verified, no JS shell). Uniquely covers the present-day architecture: the Grid (2x4 lane routing, splits/merges, block types
- `neural-dsp-quad-cortex-01-manual.txt` — **Quad Cortex User Manual 2.0.0 (CorOS 2.0.0) [PDF]** (20,930 words, pdf)  
  Official PDF, 10.6 MB. The mid-life snapshot of the UI, and the most useful comparison point against the 4.0.0 manual because the interaction model changed materially here: HYBRID mode and per-row mode assignment are documented as new, Neural Capture banks wer
- `neural-dsp-quad-cortex-02-manual.txt` — **Quad Cortex User Manual 1.4.0 (CorOS 1.4.0) [PDF]** (20,795 words, pdf)  
  Official PDF, 7.4 MB. The earliest widely-available manual -- the original shipped UI before HYBRID mode, before search/filter on Captures, and before cloud sync matured. Uniquely preserves the banked Neural Capture organization scheme and the launch-era Direc
- `neural-dsp-quad-cortex-03-quickstart.txt` — **Quad Cortex Quick Start Guide [PDF]** (3,253 words, pdf)  
  Official 15-page PDF. The only doc that shows the intended out-of-box onboarding path -- first power-on, Wi-Fi/account pairing, the connection diagrams for the four rig topologies (amp front-end, 4-cable method, FRFR, direct/studio), and I/O impedance and leve
- `neural-dsp-quad-cortex-04-firmware-changelog.txt` — **CorOS 4.0.0 and Cortex Control 4.0.0 release notes (Jan 21, 2026)** (1,028 words, html)  
  Newest major release notes. Documents the version-unification decision (CorOS and Cortex Control now share one version number, one release train for Quad Cortex and Quad Cortex mini), the new Custom Device Name identity feature for multi-device rigs, and the H
- `neural-dsp-quad-cortex-05-firmware-changelog.txt` — **CorOS 3.3.0 / Cortex Control 1.4.0 release notes -- Neural Capture V2 (Nov 26,** (2,451 words, html)  
  ~15k characters. The single most important capture-engine changelog: introduces Neural Capture V2 alongside V1 and explains how the two coexist, plus 29 new virtual devices. Critical for a competing product because it shows how a vendor versions a modeling eng
- `neural-dsp-quad-cortex-06-firmware-changelog.txt` — **CorOS 3.2.0 / Cortex Control 1.3.0 release notes (Aug 7, 2025)** (1,331 words, html)  
  ~8k characters covering the 3.x maturity phase. Distinct from the majors: this is a pure workflow/stability release, so its fixed-bug list is unusually revealing about the long-tail defects a deep hardware+desktop-app product accumulates -- sync races between 
- `neural-dsp-quad-cortex-07-firmware-changelog.txt` — **CorOS 3.0.0 and Cortex Control 1.1.0 release notes (Jul 30, 2024)** (1,596 words, html)  
  ~9.4k characters. The major that established the hard coupling between firmware and desktop app ('CorOS 3.0.0 is required to run Cortex Control 1.1.0') -- documents the version-lockstep dependency that 4.0.0 later resolved by unifying version numbers. Useful a
- `neural-dsp-quad-cortex-08-firmware-changelog.txt` — **Cortex Control public release + CorOS 2.3.0 release notes (Jan 2, 2024)** (2,750 words, html)  
  ~16k characters. The launch changelog for the companion desktop editor -- the moment the product went from device-only to device-plus-app. Uniquely documents the initial desktop/device sync contract, what the app could and could not edit at 1.0.0, and the conn
- `neural-dsp-quad-cortex-09-firmware-changelog.txt` — **CorOS 2.0.0 release notes (Jan 23, 2023)** (3,101 words, html)  
  ~18k characters -- the longest changelog in the set. The pivotal UX overhaul: HYBRID mode and per-row footswitch mode assignment, removal of Neural Capture banks in favor of sort/filter/search, global Cab/IR/Capture bypass, scroll-performance work and the scro

  > All 10 URLs were fetched and verified, not just search-result guesses. PDFs returned content-type: application/pdf with real byte counts; HTML pages were fetched with curl, stripped of script/style, and confirmed to contain the actual document text in the served markup (no JS shell). IMPORTANT GAP -- no PDF manual exists for CorOS 3.x or 

### Neural DSP Nano Cortex (NanOS firmware + Cortex Cloud companion app)

- `neural-dsp-nano-cortex-00-manual.txt` — **Nano Cortex® User Manual 2.2.0** (9,337 words, html)  
  The single most complete architecture document: full signal path (input gate → pre-effects → capture → IR loader → post-effects), Performance Mode vs Capture Mode state machine, footswitch/LED behavior table, LEVEL knob and parameter-lock semantics, the comple
- `neural-dsp-nano-cortex-01-firmware-changelog.txt` — **Massive NanOS 2.0.0 update is now available! (April 23, 2025)** (1,165 words, html)  
  The architectural pivot release, and the highest-signal design document in the set: Nano Cortex shipped with a FIXED effects chain and 2.0.0 replaced it with a user-customizable chain, added a global Input Gate before pre-effects slot 1, IR Global Bypass, and 
- `neural-dsp-nano-cortex-02-firmware-changelog.txt` — **NanOS 2.1.0 is now available (July 24, 2025)** (631 words, html)  
  Records the control-surface expansion AND the crash class that forced it: incoming MIDI CC for per-slot bypass, expression position, tap tempo and tuner; on-device tap tempo gesture (hold FSW I, exit with FSW II); per-output IR Loader bypass; MIDI Thru; global
- `neural-dsp-nano-cortex-03-firmware-changelog.txt` — **NanOS 2.2.0 is now available — Neural Capture V2 Player, Tremolo, summed outpu** (1,150 words, html)  
  The richest bug-history document. Adds Neural Capture V2 playback (higher-resolution captures trained in Cortex Cloud on Quad Cortex — a format-versioning story worth studying), offline Cortex Cloud mode, capture auditioning in-cloud before download, up to fiv
- `neural-dsp-nano-cortex-04-firmware-changelog.txt` — **NanOS 2.2.1 is now available (December 10, 2025)** (149 words, html)  
  Newest NanOS as of August 2026 — confirms the firmware ceiling and documents a boot-blocking regression: the device could fail to boot entirely when an expression or MIDI pedal was connected. Short, but it is the only record that 2.2.0 shipped a peripheral-enu
- `neural-dsp-nano-cortex-05-reference-sheet.txt` — **Nano Cortex device list** (1,066 words, html)  
  Machine-greppable table of every virtual device — factory Neural Captures, IR loader, overdrives (guitar and bass split), delay, reverb, compressor, pitch, modulation, filter, EQ, wah, utility — with the real-world unit each models AND an 'Added in NanOS' colu
- `neural-dsp-nano-cortex-06-faq.txt` — **Nano Cortex® F.A.Q. (official support)** (2,749 words, html)  
  States the product's hard limits and refusals in a way the manual does not: what Neural Capture actually models and what it cannot, capacity ceilings (25 factory + 256 user captures), signal-chain and preset constraints, connectivity/compatibility answers, and
- `neural-dsp-nano-cortex-07-written-guide.txt` — **Using the Cortex Cloud app with Nano Cortex** (2,255 words, html)  
  The companion-app UX walkthrough as a task flow rather than a feature list — the most directly transferable document for anyone designing a phone app that drives a tone engine. Covers pairing, browsing and downloading cloud content into limited device slots, m
- `neural-dsp-nano-cortex-08-written-guide.txt` — **Using MIDI on Nano Cortex** (2,157 words, html)  
  Full MIDI integration reference in one place: sending and receiving over both USB MIDI and TRS MIDI, PC vs CC responsibilities, per-slot bypass and expression-position CC mapping, MIDI Thru chaining, clock source selection, and the TRS wiring/adapter expectati
- `neural-dsp-nano-cortex-09-written-guide.txt` — **How to create a Neural Capture on Nano Cortex** (3,863 words, html)  
  Longest guide in the set (~22k chars) and the only end-to-end description of the capture pipeline as a user experiences it: physical connection diagrams, calibration/level-setting step, the tube-amp and analog-boost safety warnings, what the training pass actu

  > VERIFICATION: every URL above was fetched with plain curl and confirmed to return HTTP 200 with the full article text present in the served HTML. Both neuraldsp.com and support.neuraldsp.com are Next.js with server-side rendering, so `curl | html2text` (or any stripper) yields complete greppable text with no JS execution. Byte counts of e

### HeadRush Prime / Core / Flex Prime (HeadRush OS 5.1.0)

- `headrush-prime-core-fl-00-firmware-changelog.txt` — **READ ME - HeadRush Prime Firmware Update Instructions v5.1.0 (cumulative chang** (5,017 words, pdf)  
  THE highest-signal document in the set. Despite the 'Update Instructions' filename, pages 3-13 are a complete cumulative release-note history for every Prime firmware from 3.1.1 through 5.1.0, with explicit 'Improvements & Bug Fixes' sections. Records what shi
- `headrush-prime-core-fl-01-firmware-changelog.txt` — **READ ME - HeadRush Core Firmware Update Instructions v5.1.0 (cumulative change** (4,618 words, pdf)  
  Core's own cumulative changelog, 3.1.1 -> 5.1.0 (530 lines). Not a duplicate of the Prime file: Core has a distinct MIDI section (5-message command chains for PC/CC), a different 4.x history (no 4.1.1 release - Core jumped 4.1.0 -> 5.0.0), and Core-specific fi
- `headrush-prime-core-fl-02-firmware-changelog.txt` — **READ ME - HeadRush Flex Prime Firmware Update Instructions v5.1.0 (cumulative ** (3,340 words, pdf)  
  Flex Prime changelog, 4.0.0 -> 5.1.0 (378 lines). Starts at 4.0.0 because the Flex Prime launched into an already-mature OS - so it uniquely shows what a newest-hardware-variant inherits versus what has to be re-fixed per chassis. Contains the Bluetooth/Wi-Fi 
- `headrush-prime-core-fl-03-manual.txt` — **HeadRush Prime User Guide v5.1.0** (25,492 words, pdf)  
  Newest flagship manual, 86 pages / ~25k words of extractable text. The most complete description of the HeadRush information architecture: Main screen, Menu screen, Rigs (signal path, stereo vs mono, reverb/delay tail spillover, amp/cab doubling), five distinc
- `headrush-prime-core-fl-04-manual.txt` — **HeadRush Core User Guide v5.1.0** (24,439 words, pdf)  
  81 pages / ~24k words. Uniquely shows how the same OS collapses onto a smaller control surface: Core has fewer footswitches and adds a Bank A/B paging scheme, so this manual documents the compromises made when the same rig/scene/setlist model must drive less h
- `headrush-prime-core-fl-05-manual.txt` — **HeadRush Flex Prime User Guide v5.1.0** (21,890 words, pdf)  
  74 pages / ~22k words - the smallest of the three, and the differences are the point. Flex Prime ships without the onboard mic input, so this manual documents the cloud-only / ReValver-only path to amp clones rather than on-device capture. Reading it against t
- `headrush-prime-core-fl-06-manual.txt` — **HeadRush Prime User Guide v4.1.0 (pre-5.0 UI baseline)** (21,525 words, pdf)  
  Included deliberately as a before/after pair with the 5.1.0 guide, because the UI changed materially at 5.0. Verified by term counts: 'Drum Machine' appears 0 times in 4.1.0 and 70 times in 5.1.0; 'TONE3000' 0 vs 13. This is the last manual showing the navigat
- `headrush-prime-core-fl-07-faq.txt` — **HeadRush Flex Prime | Frequently Asked Questions** (4,034 words, html)  
  Server-side rendered, ~23.6k chars of body text. Contains the explicit cross-model comparison section ('main differences between the Flex Prime and Prime/Core') that appears in no manual, plus answers the manuals dodge: where the Amp Cloner went on a unit with
- `headrush-prime-core-fl-08-faq.txt` — **HeadRush Core | Frequently Asked Questions** (3,832 words, html)  
  Server-side rendered, ~22.5k chars. The Core-side counterpart with its own 'main differences between the Prime and Core' section and the Bank A/B footswitch questions. Also documents footswitch auto-assignment behavior and how to turn it off - an opinionated d
- `headrush-prime-core-fl-09-written-guide.txt` — **HeadRush Prime | Troubleshooting the Amp Cloner** (968 words, html)  
  Server-side rendered, ~5.5k chars. The only official document that states the hard limits of HeadRush's neural capture feature rather than marketing it: gated fuzz pedals cannot be cloned (bias-shift behavior reads as a noise gate), vintage high-Z fuzzes need 

  > VERIFICATION: every URL was fetched with raw curl, not just search-result-trusted. All seven PDFs return 200 / application/pdf with non-zero bodies and were downloaded and run through pdftotext -layout to confirm real extractable text (not scanned images). All three HTML pages return 200 / text/html and were confirmed to contain the full 

### Line 6 Helix Stadium / Helix Stadium XL (with classic Helix + HX Edit for comparison)

- `line-6-helix-stadium-h-00-manual.txt` — **Helix Stadium & Stadium XL Owner's Manual (Rev D, firmware v1.3.x)** (452 words, html)  
  The primary architecture document for the Stadium platform and the only edition covering the current firmware. Server-side rendered across 93 English pages, each 1,300-4,600 words. Uniquely defines: the block/DSP model ('The Blocks'), the Presets/Setlists/Temp
- `line-6-helix-stadium-h-01-firmware-changelog.txt` — **Helix Stadium 1.3.2 Release Notes (covers 1.3.2, 1.3.1, and 1.3)** (5,338 words, html)  
  Highest-signal document in the set — ~108KB of server-side text, roughly 100 individually described FIXED entries across three releases, naming the exact failure mode each time. This is the shipped-broken record: DSP errors when switching between presets with 
- `line-6-helix-stadium-h-02-firmware-changelog.txt` — **Helix Stadium 1.2.1 Release Notes (covers 1.2.1 and 1.2)** (3,575 words, html)  
  The earliest Stadium changelog Line 6 published, and the only record of the Showcase playback/automation engine's original introduction in 1.2 (Dec 2025) plus the stability round that immediately followed in 1.2.1 (Jan 2026). Covers the launch-era defect class
- `line-6-helix-stadium-h-03-firmware-changelog.txt` — **Helix/HX 3.80 Release Notes** (1,714 words, html)  
  The classic-Helix-line changelog, for comparison against Stadium. ~90KB server-side text with its own long FIXED list spanning the whole HX family (Helix Floor/Rack/LT, HX Stomp, HX Stomp XL, HX Effects, HX One) plus HX Edit. Shows which defect categories are 
- `line-6-helix-stadium-h-04-manual.txt` — **Helix 3.80 Owner's Manual (English, Rev G)** (38,426 words, pdf)  
  Verified application/pdf, 14.6 MB, 38,411 extractable words — the newest and fullest manual for the original Helix Floor/Rack. This is the pre-Stadium design baseline: the older signal-path/DSP model, the 4-preset-bank footswitch grammar, the original Snapshot
- `line-6-helix-stadium-h-05-manual.txt` — **HX Edit Pilot's Guide 3.80 (English)** (47,612 words, pdf)  
  Verified application/pdf, 22.5 MB, 47,588 extractable words — the deepest editor-application document in the whole category and the requested HX Edit documentation. Covers the desktop editor's full model: preset/setlist library management, IR library slots and
- `line-6-helix-stadium-h-06-manual.txt` — **Helix Stadium Native Owner's Manual (v1.3.3, Rev A)** (336 words, html)  
  The plugin edition of Stadium, and the only place Line 6 writes the DSP budget down explicitly. Its 'Hardware Compatibility DSP Limits' page (/hardware-compatibility-dsp-limits) states the Dynamic DSP rules and gives a Block Type / Maximum Allowable Block Inst
- `line-6-helix-stadium-h-07-reference-sheet.txt` — **Helix Stadium XL Cheat Sheet (English)** (1,301 words, pdf)  
  Verified application/pdf, 5.1 MB, 1,299 extractable words. The XL-specific hardware quick reference: footswitch layout and count, the press/hold/multi-switch gesture table, Output Volume knob assignment, and rear-panel I/O — the physical control surface the XL
- `line-6-helix-stadium-h-08-reference-sheet.txt` — **Helix Stadium Floor Cheat Sheet (English)** (1,232 words, pdf)  
  Verified application/pdf, 3.3 MB, 1,232 extractable words. Same reference for the smaller Floor SKU. Grep it against the XL sheet to see how Line 6 collapses the same feature set onto fewer switches — the Combo footswitch mode, FS1+FS7 chords, and mode-switchi
- `line-6-helix-stadium-h-09-written-guide.txt` — **Updating to Helix Stadium/XL from Helix/HX devices** (488 words, html)  
  A short but unusually candid migration document — the vendor enumerating its own compatibility breaks. States that Stadium cannot import full-state backups (only presets, IRs, and favorites extracted individually), that the legacy Hybrid cab engine was dropped

  > VERIFICATION METHOD — every URL was actually fetched, not just found. The four PDFs were downloaded (curl -sL), confirmed as `PDF document, version 1.7` via file(1), and run through pdftotext to prove the text layer is extractable rather than scanned: Helix 3.80 manual 38,411 words, HX Edit Pilot's Guide 47,588 words, XL cheat sheet 1,299

### Darkglass Anagram (KosmOS firmware / Darkglass Suite)

- `darkglass-anagram-kosm-00-manual.txt` — **Anagram Manual — KosmOS 1.17 (official, current)** (7,138 words, pdf)  
  The authoritative 97-page current manual, verified as real embedded text (not scanned). Only source that fully specifies the interface model: Core Concepts, Modes and Views, Binding Settings, Preset Manager, Tuner & Tempo, Looper, Mixer, Global EQ, the full Bl
- `darkglass-anagram-kosm-01-firmware-changelog.txt` — **Darkglass Suite & Software — KosmOS 1.17.1 changelog (official, live)** (3,353 words, html)  
  The only official public changelog page, server-rendered. Carries 1.17.1 and 1.17.0: the TONE3000 block, removal of the fixed 3-NAM ceiling in favor of a DSP budget, Backup/Restore, preset/scene MIDI Out, the full looper CC map (CC110-CC118), MIDI Through sett
- `darkglass-anagram-kosm-02-firmware-changelog.txt` — **KosmOS 1.11.1 release notes (archived official Suite page)** (3,031 words, html)  
  The 1.11.1 entry, which is pure stability regression data and exists nowhere else: 'Fix multiple cases where Anagram sometimes freezes', audio-backend crash when scrolling through cabinet files or neural models, audio-backend crash under sustained heavy load, 
- `darkglass-anagram-kosm-03-firmware-changelog.txt` — **KosmOS 1.12.0 release notes (archived official Suite page)** (3,043 words, html)  
  The 1.12.0 entry: new expression-pedal blocks (Volume Pedal, Wauwa wah, Pitch Bender) and the breaking change to Expression Binding semantics — binding parameters are deliberately excluded from presets/scenes and are re-read from live pedal position on preset 
- `darkglass-anagram-kosm-04-firmware-changelog.txt` — **KosmOS 1.13.0 release notes (archived official Suite page)** (2,993 words, html)  
  The 1.13.0 entry: the Ignissor multiband compressor and the Input Gain feature, including its four separate entry points into the UI (knob 1 long-press, Tuner & Tempo screen, Mixer screen, Audio & Exp settings) and six renameable 'Instrument' gain profiles at 
- `darkglass-anagram-kosm-05-firmware-changelog.txt` — **KosmOS 1.14.0 / 1.14.1 release notes (archived official Suite page)** (3,084 words, html)  
  The densest interface-regression record in the whole set. Includes 'Disable touch control on continuous parameters (regression in v1.9)' — a touch/gesture conflict they shipped and later reverted — plus Marketplace support, file lists widened to two slots on l
- `darkglass-anagram-kosm-06-firmware-changelog.txt` — **Darkglass KosmOS 1.16 release announcement (vendor press release)** (1,643 words, html)  
  Fills the 1.16 gap, which has no surviving official changelog page. Carries the vendor's own release text for the NAM Architecture 2 milestone: the TONE3000 partnership, the three dedicated blocks (Neural Amp, Neural Pedal, Neural Loader) and the then-current 
- `darkglass-anagram-kosm-07-manual.txt` — **Anagram Manual, original launch edition (Block List v1.1, April 2025)** (5,532 words, html)  
  The pre-Shopify HTML manual from launch, fully server-rendered (~34K chars of text). Included because the UI changed materially: this edition's contents are 'General operation / Dedicated functions / System blocks / Block List (v1.1)', whereas the 1.17 manual 
- ~~Darkglass Anagram Quickstart Guide (Sweetwater SweetCare, updated Aug 2026)~~ — **download failed**
- `darkglass-anagram-kosm-09-written-guide.txt` — **Darkglass Anagram Now Connects Directly to TONE3000 (TONE3000, Aug 2026)** (1,175 words, html)  
  The integration described from the TONE3000 side rather than the device side. Documents the in-Suite browse-and-load workflow, the Tonepack packaging concept, and TONE3000's own framing of NAM A2 capacity on Anagram versus other hardware — the partner-facing v

  > All ten URLs were fetched and verified: item 1 returns a real `application/pdf` (4.3 MB) whose text extracts cleanly via pypdf (97 pages, 3.5K chars in the first 8 pages alone); items 2-10 return HTTP 200 with the substantive text present in the served HTML (verified by stripping tags and confirming the changelog/manual prose is there, no

### Footswitch/preset UX references: Dimehead NAM Player, Morningstar MC6/MC8 (+ PRO), Fractal

- `footswitch-preset-ux-r-00-firmware-changelog.txt` — **Dimehead NAM Player — Firmware Versions and Updates (full changelog v0.9.8 → v** (1,160 words, html)  
  VERIFIED server-rendered (93KB HTML, full text in source, no JS shell). This is the only real Dimehead engineering document that exists and it is the highest-signal item in the whole set: a dated, per-version log of what a shipping NAM hardware pedal added and
- `footswitch-preset-ux-r-01-reference-sheet.txt` — **Dimehead — Explore the MIDI Mapping of Your NAM Player** (377 words, html)  
  VERIFIED server-rendered; the complete CC map is literal text in the HTML. This is the de-facto parameter spec for the product in the absence of a manual: it exposes the whole signal chain and every automatable parameter with its CC number — Volume #14, Room #
- `footswitch-preset-ux-r-02-faq.txt` — **Dimehead NAM Player — FAQ** (1,197 words, html)  
  VERIFIED server-rendered (101KB, full Q&A text in source). The only place Dimehead documents the hard design constraints of running NAM on hardware, which no other vendor doc in this set covers: the input-gain-trim calibration table mapping reamp level to gain
- `footswitch-preset-ux-r-03-reference-sheet.txt` — **Morningstar — Action Types (footswitch gesture glossary + action execute seque** (852 words, html)  
  VERIFIED server-rendered (~4.9KB of dense prose, fully in the served HTML). This is THE document the brief is really asking for — the industry's most explicit footswitch gesture vocabulary, on one page: Press, Release, Long Press, Long Press Release, Double Ta
- `footswitch-preset-ux-r-04-reference-sheet.txt` — **Morningstar — Message Types (complete command vocabulary)** (5,479 words, html)  
  VERIFIED server-rendered (~32K chars of text). The other half of the gesture model: what a gesture can DO. Full taxonomy of ~50 message types including the controller-native ones a competing preset/chain app would need to invent — Engage Preset (nested preset 
- `footswitch-preset-ux-r-05-manual.txt` — **Morningstar MC6 PRO User Manual (current generation, colour per-switch display** (6,030 words, html)  
  VERIFIED server-rendered (~34K chars, complete manual body in the served HTML — this is the official manual, not a JS reader). Newest-generation controller UI. Uniquely covers the state model behind the gestures: Profiles, Banks/Pages/Presets hierarchy, Toggle
- `footswitch-preset-ux-r-06-manual.txt` — **Morningstar MC8 User Manual (previous generation, single LCD, 8 switches)** (5,034 words, html)  
  VERIFIED server-rendered (~29K chars). Included deliberately as the OLDER UI generation, because the display model changed materially: one shared LCD driving 8 switches versus the PRO's per-switch colour displays. Grepping the two side by side shows what a ven
- `footswitch-preset-ux-r-07-manual.txt` — **Morningstar Editor User Manual (the desktop/web editor for MC6/MC8/PRO)** (5,358 words, html)  
  VERIFIED server-rendered (~31K chars). This is the editor manual specifically requested, and it is the closest analogue in the industry to a companion app editing an ordered chain of switch assignments. Covers the editor's information architecture and bulk-edi
- `footswitch-preset-ux-r-08-firmware-changelog.txt` — **Morningstar MC6 PRO — Firmware Release Log (v3.11.2 → v3.14.0)** (3,264 words, html)  
  VERIFIED server-rendered: GitHub renders release bodies into the HTML (~21K chars of changelog text extracted with JS stripped). Morningstar's official firmware changelog, and the highest-signal document on the Morningstar side because it is a confession list 
- `footswitch-preset-ux-r-09-manual.txt` — **Fractal Audio Axe-Fx III Owner's Manual (July 2022, current as of firmware 20.** (53,440 words, pdf)  
  VERIFIED: HTTP 200, application/pdf, 5.4 MB, %PDF-1.7, text layer extracts cleanly with pdftotext (Title: 'Axe-Fx III Owner's Manual'). Contains exactly the chapters requested. Ch.6 SCENES & CHANNELS (p.43): Overview, Changing Channels, Setting Up Channels, Se
- `footswitch-preset-ux-r-10-reference-sheet.txt` — **Fractal Audio Footswitch Functions Guide v1.3 (April 2022, FM9/FM3/FC-12/FC-6)** (10,817 words, pdf)  
  VERIFIED: HTTP 200, application/pdf, 762 KB, 25 pages, text layer extracts (Title: 'Fractal Audio Systems Footswitch Functions Guide'). Fractal's answer to Morningstar's Action Types, and the complement to the Axe-Fx III manual: the exhaustive per-function ref

  > DIMEHEAD: CONFIRMED — no manual exists, in any format. I fetched and grepped dimehead.de, /product/nam-player/, /faq/, /firmware/, and /midi-mapping/; there is not a single .pdf link anywhere on the site, and the word "manual" never appears in the FAQ or product page. /sitemap_index.xml returns 404. The site's entire nav is Home / Videos 

---

**Total: 437,275 words across 60 documents.**

Raw sources are in `src/` and extracted text in `text/`. Both are gitignored —
third-party copyrighted documentation kept locally as study material. This
index is committed so the folder explains itself on a fresh clone.

