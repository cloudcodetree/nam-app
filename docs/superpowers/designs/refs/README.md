# Competitor UI reference

Private design reference: how other modelers actually present their interfaces.
Study material, not assets to reuse. Collected 2026-08-22.

Companion to `../how-others-work.html` (interaction mechanics) and
`../../research/2026-08-22-modeler-ux-research.md` (the full corpus).

Note: several files arrived as PNG bytes behind a `.svg` URL (a Contentful
`?fm=png` transform). Extensions here match the REAL file type, not the URL.


## Neural DSP Quad Cortex — Grid signal-chain editor, Gig View, scene pages, Cortex

- `neural-dsp-quad-cortex-grid-00.png` — *screen-ui* — The Grid on the device itself, 800x480 native screen res: preset "1A Brit 2203", two active signal rows of block tiles (each block a rounded square with a colour-coded category outline and a line-art 
- `neural-dsp-quad-cortex-grid-01.png` — *screen-ui* — Gig View in SCENE mode — the scene page: 8 large footswitch tiles (A-H) in a 4x2 grid, each with a big scene name ("British 2203", "+MX OD +Doubler", "Dry Double", "Solo Boost"...), per-scene colour, 
- `neural-dsp-quad-cortex-grid-02.png` — *screen-ui* — Gig View in STOMP mode — 4x2 footswitch tiles showing the device assigned to each switch (Crying Wah, MX ClassicOD 4, Brit Governor, Rodent Drive, Looper X, Transpose, Multiple devices (2), Room), eac
- `neural-dsp-quad-cortex-grid-03.png` — *screen-ui* — Gig View in PRESET mode — 4x2 tiles of preset slots 1A-1H with bank-number + colour-coded letter and multi-line preset names (Brit 2203, Brit Plexi100 Normal, US TWN Vibrato, Rols Jazz CH120...), curr
- `neural-dsp-quad-cortex-grid-04.png` — *screen-ui* — Gig View in HYBRID mode — top row is 4 scene tiles (A-D, tinted per scene), bottom row is 4 stomp tiles (E-H, with device icons); good example of one screen mixing two tile grammars in a single 4x2 la
- `neural-dsp-quad-cortex-grid-05.png` — *app-ui* — Cortex Control desktop app (1479x1114): full Grid editor with two populated chain rows, GRID/DIRECTORY tabs, A-H scene selector strip in the header, left rail of device categories (Plugins, Amp, Neura
- `neural-dsp-quad-cortex-grid-06.png` — *app-ui* — Cortex Control desktop app: Grid editor above, amp-block parameter editor docked below (Gain/Bass/Mid/Treble/Presence/Master/Output knobs with numeric readouts), plus a searchable amp-model list in th
- `neural-dsp-quad-cortex-grid-07.png` — *screen-ui* — Very high-res (3200x1920) render of the CorOS 4.0 Grid screen: four rows, two independent inputs, parallel branches with visible split/merge routing lines, coloured block tiles, small stomp-assignment
- `neural-dsp-quad-cortex-grid-08.png` — *screen-ui* — Device-picker overlay on the device: left icon rail of block categories, a GUITAR/BASS tab pair, and a scrolling alphabetical list of amp models sliding over a dimmed Grid with an empty '+' slot behin
- `neural-dsp-quad-cortex-grid-09.png` — *screen-ui* — On-device block parameter editor page: header with block type (GUITAR AMP) + model name in red, scene A stepper, bypass and confirm buttons; body is a grid of parameter cells, each a label + red arc k
- `neural-dsp-quad-cortex-grid-10.png` — *screen-ui* — The scene-selector dropdown open over the Grid: preset title "6E Amp & Room", a height-capped list of scenes A-H with coloured letter chips and editable names (Clean, Clean+Delay, Distorted, Delay, Le

> Source and method: all links come from the official Neural DSP CorOS 4.0.0 manual (https://neuraldsp.com/manual/quad-cortex), the Quad Cortex mini manual, and the Gig View development-update post. I curl'd the raw HTML of each and extracted the Contentful CDN (images.ctfassets.net) asset URLs, then downloaded and visually inspected every image in this list — none is guessed, every one is confirmed

## HeadRush Prime / Flex Prime / Core (HeadRush FX touchscreen amp modeller & multi

- `headrush-prime-flex-prime-co-00.png` — *device-with-screen* — Flex Prime rig view, shot dead-on and fully legible: two-row signal chain of photo-real pedal/amp blocks with an active green-lit block, IN and OUT terminals at the edges, TAIL toggle and grid-view bu
- `headrush-prime-flex-prime-co-01.webp` — *screen-ui* — Block detail page, native full-screen capture at 1180x786: 'ELEVEN REVERB' header with back chevron, a photo-real pedal graphic in the left rail, PRESET and MODEL dropdowns, four parameter tiles with 
- `headrush-prime-flex-prime-co-02.webp` — *screen-ui* — Hands-Free Mode screen, native full-screen capture: 'HANDS-FREE MODE' title bar with CPU meter, the block name 'CHORUS', and an oversized horizontally-paged parameter carousel showing DEPTH 58% centre
- `headrush-prime-flex-prime-co-03.png` — *screen-ui* — Rig list: a two-panel official support screenshot showing the collapsed rig title bar on top and, below, the pulled-down rig list overlay — search field, SORT control with up/down arrows, and rows '01
- `headrush-prime-flex-prime-co-04.jpg` — *device-with-screen* — Flex Prime rig view at 1920x1080, '04-GTR-Tread Lead': full chain of photo-real blocks with one drive pedal highlighted in a yellow selection frame, amp block marked 'A', empty '+' slot at the end of 
- `headrush-prime-flex-prime-co-05.jpg` — *device-with-screen* — Flex Prime rig view close-up, '01-GTR-6505 Fat Drive*' (asterisk = unsaved edit): parallel routing with a MIX summing node joining two block rows, IN/lock icons on the left rail, OUT with a stereo ind
- `headrush-prime-flex-prime-co-06.jpg` — *device-with-screen* — Second Flex Prime rig view, '10-GTR-Parallel Vibes': a more complex parallel chain with a MIX node, two '+' add-block placeholders inline in the lower path, selected block highlighted in blue, and FIL
- `headrush-prime-flex-prime-co-07.webp` — *device-with-screen* — Rig editing gesture on the Prime 7-inch screen: a finger drag-and-dropping a block within the two-row chain, the dragged slot outlined in red and its target outlined in teal, and a full-width red 'DRA
- `headrush-prime-flex-prime-co-08.webp` — *screen-ui* — HeadRush Cloud rig browser, native full-screen capture: artist banner header (Al Joseph, '6 Rigs', bio blurb), search field with Sort and filter controls, and a four-across card grid of downloadable r
- `headrush-prime-flex-prime-co-09.png` — *device-with-screen* — Amp Clone block detail page on the Flex Prime screen: 'AMP CLONE' header with dropdown chevron, PRESET and CLONE selectors ('70s Silver Vib 8'), parameter tiles with inline fill bars (Gain, Bass, Treb
- `headrush-prime-flex-prime-co-10.webp` — *screen-ui* — Looper screen, native full-screen capture: loop timeline with layer segment dividers and playhead, PLAY state with start/end timecodes, Speed / Loop Length / Overdub Layers / Playback parameter rows, 
- `headrush-prime-flex-prime-co-11.webp` — *screen-ui* — Backing-track / practice player screen, native full-screen capture: waveform overview strip with a draggable orange loop-region selector, large detail waveform, and a TRACK / VOLUME / PITCH / TEMPO / 

> METHOD: searched, then fetched the underlying HTML with curl and grepped the raw img srcs (WebFetch summaries drop and paraphrase URLs). Every URL below was then downloaded and visually inspected before being listed, and re-checked with a HEAD request — all 12 return HTTP 200 with an image/* content-type.\n\nBEST FINDS — the four numbered assets on headrushfx.com's Prime product page (image-06 thr

## Line 6 Helix Stadium / Helix Stadium XL (2025) — device colour touchscreen UI, s

- `line-6-helix-stadium-helix-s-00.png` — *screen-ui* — Device Home > Edit screen, full-screen: four signal-flow rows of coloured block icons (split/merge nodes, amp+cab gold link icons), purple top bar with clock/undo/redo/preset name/snapshot camera/info
- `line-6-helix-stadium-helix-s-01.png` — *app-ui* — Helix Stadium desktop editor app, full window at 1652x1118 — left preset librarian sidebar (FACTORY setlist, numbered preset rows), two-row drag-and-drop chain editor with coloured block icons, bottom
- `line-6-helix-stadium-helix-s-02.png` — *app-ui* — Same Helix Stadium editor app main window with numbered annotation callouts identifying the toolbar view selectors (Home / Librarian / Command Center / Global EQ / Songs), device-connection indicator,
- `line-6-helix-stadium-helix-s-03.png` — *screen-ui* — Snapshot panel open over the device Home > Edit screen: flyout list of SNAPSHOT 1-8 with camera-numbered icons, right sidebar with Rename/Color, Copy Snapshot, Paste Snapshot, Discard Edits toggle, an
- `line-6-helix-stadium-helix-s-04.png` — *device-with-screen* — Helix Stadium XL scribble strips in Snapshot mode — two rows of OLED strips above the footswitches reading BANK, SNAPSHOT 1-8, STOMP A/PRESET/MORE and TAP/TUNER, with the corresponding ringed footswit
- `line-6-helix-stadium-helix-s-05.png` — *screen-ui* — Helix Stadium (non-XL) Home > Play view in Snapshot footswitch mode — the on-screen 6x2 grid of soft footswitch tiles standing in for scribble strips: BANK up/down, named colour-coded snapshot tiles (
- `line-6-helix-stadium-helix-s-06.png` — *screen-ui* — Helix Stadium Home > Play view in Stomp A footswitch mode — the same tile grid showing per-effect stomp assignments and bypass/lit states, the direct competitor to a 'scene'/'gig view' page. 640x400.
- `line-6-helix-stadium-helix-s-07.png` — *screen-ui* — Focus View on the device for a Reverb block — full-bleed photographic image of the modelled pedal with five named morph zones (Reflection, Pad, Slap, Edges, WobbleSlap), draggable lens icon, in/out le
- `line-6-helix-stadium-helix-s-08.png` — *screen-ui* — Device Model Browser in Gallery view — search field, colour-coded category rail (Favorites/Amp/Preamp/Cab/Distortion/Delay/Reverb), 2-up grid of photographic model thumbnails with mono/stereo glyphs, 
- `line-6-helix-stadium-helix-s-09.png` — *app-ui* — Helix Stadium Native plugin window — Line 6 header, A/B compare, preset name bar, two-row signal chain with input/output meters flanking, and the lower half split between model browser list and the re
- `line-6-helix-stadium-helix-s-10.png` — *screen-ui* — Device Song View (Showcase engine) — multitrack waveform lanes (Guitar, AcousticGtr, Bass, Drums, Keys, Horns, Other) with bar ruler, green flag markers, loop region, 'Next' song queue, tempo/time sig
- `line-6-helix-stadium-helix-s-11.png` — *app-ui* — Command Center view in the Helix Stadium app (XL layout) — per-footswitch and per-expression-pedal command assignment surface, showing how Line 6 lays out the footswitch matrix in the editor. 1648x972

> Source and method: everything came from Line 6's own online Helix Stadium manual (manuals.line6.com/en/helix-stadium/live/...). I fetched the raw HTML of the display, snapshots, the-focus-view, the-model-browser, signal-path-routing, song-view, quick-start, librarian-view and helix-stadium-edit-application pages and pulled the <img> src attributes out of them. All twelve URLs were re-verified with

## Neural DSP Nano Cortex + Cortex Cloud app (the Nano Cortex's editor; there is no

- `neural-dsp-nano-cortex-corte-00.png` — *app-ui* — THE key image. Full-bleed phone render of the Cortex Cloud 'Pedal' tab FX chain editor for Nano Cortex: Pedal/Library segmented toggle in header, INPUT GATE slider row at 62.4%, PRE group (Compressor 
- `neural-dsp-nano-cortex-corte-01.png` — *manual-diagram* — Three-panel anatomy of the FX chain: 'EFFECTS OFF' (all slots collapsed to grey pills) vs 'EFFECTS ON' (slots expanded with colour fill, glyph tile, name + preset sub-name, power button) vs 'FX BLOCK 
- `neural-dsp-nano-cortex-corte-02.png` — *manual-diagram* — Second variant of the same off/on/params triptych with a different chain loadout (Noise Gate, Pitch Shifter / Modulation, Delay, Reverb) and a Transpose MIX/SEMITONES knob pair. Useful as a second sam
- `neural-dsp-nano-cortex-corte-04.png` — *manual-diagram* — Neural Capture detail/tone page with callouts: back chevron + centred title + edit-pencil + overflow menu header, owner/name card, artwork panel, description + created date, Local/Upload-to-Cloud row,
- `neural-dsp-nano-cortex-corte-05.png` — *app-ui* — Neural Capture slot list in the app: NEURAL CAPTURE header with bypass power button, CAPTURE VOLUME slider row with dB readout, then five numbered capture slots as rounded pills; the active slot swaps
- `neural-dsp-nano-cortex-corte-06.png` — *app-ui* — Preset management, two views side by side: 'FOOTSWITCH ASSIGNMENTS' showing IA/IB/IIA/IIB slots with the selected one inverted to a white card + overflow menu, and 'ALL PRESETS' as a numbered scrollin
- `neural-dsp-nano-cortex-corte-07.png` — *manual-diagram* — Full-screen Tempo modal inside the phone frame: title + close X header, giant green 120 BPM readout, large TAP TEMPO pad, and a bottom row with a tempo knob and a Global/Preset scope toggle plus expla
- `neural-dsp-nano-cortex-corte-08.png` — *manual-diagram* — The fixed seven-slot signal chain rendered as a horizontal strip of rounded glyph tiles with coloured borders: 2 pre-FX (yellow), Neural Capture and IR (grey), 3 post-FX (blue), with the input-gate st
- `neural-dsp-nano-cortex-corte-09.png` — *app-ui* — A real Cortex Cloud capture-wizard step screen: dark rounded sheet, instruction line with the key term coloured green ('Plug your instrument into Input 1'), photo of the pedal's rear jacks with a whit
- `neural-dsp-nano-cortex-corte-10.png` — *manual-diagram* — Labelled top-panel diagram of the Nano Cortex hardware itself (the unit has no display, only knobs and LED rings): Capture Gain, Bank/Capture-slot LED rows, 3-band EQ, FX AMOUNT knob, IR Loader slot L
- `neural-dsp-nano-cortex-corte-11.png` — *screen-ui* — App Store screenshot, Cortex Cloud 'My Feed': feed rows of published presets, each with author avatar + byline + timestamp + download button, and a wide dark thumbnail that renders the preset's whole 
- `neural-dsp-nano-cortex-corte-12.png` — *screen-ui* — App Store screenshot, Cortex Cloud 'Search': search field, four pill filter tabs with glyphs, All + 'Most popular' sort row, then sectioned results (Presets, Neural Captures) as list rows with a chain

> Naming correction worth knowing: there is no Neural DSP product called "Cortex Control". The Nano Cortex is edited from the **Cortex Cloud** app (iOS/Android, shared with Quad Cortex; App Store id1484938666). Neural DSP ships **no official desktop editor** — a PC editor is an open feature request on their forum, and the only desktop options are unofficial third-party ones (a "Nano Cortex Presets C

## Darkglass Anagram (KosmOS)

- `darkglass-anagram-kosmos-00.jpg` — *device-with-screen* — Dead-on top-down product render at 4355x2449 with the full KosmOS chain view legible: preset chip "01A", preset name "Preset 1", six chain blocks with per-block pedal artwork and signal wire, right-ha
- `darkglass-anagram-kosmos-01.webp` — *screen-ui* — Tight close-up of the chain editor screen filling the frame: six pedal-thumbnail blocks (Muff-style, Alpha-Omicron, Plate Reverb, Jim Bass preamp) on the horizontal signal wire, plus the "chain | pres
- `darkglass-anagram-kosmos-02.jpg` — *device-with-screen* — 2000x900 official render of the whole chain/blocks screen at an angle: "01A Preset 1" header, Chain/Preset toggle, kebab menu, five blocks (Microtubes B3K, Harmonic Booster, a utility block, Amp+EQ, P
- `darkglass-anagram-kosmos-03.jpg` — *device-with-screen* — Straight-on 1280x720 frame of the black Guitar Essentials unit showing the chain view with a user-named preset: "01A PEGGY CRUNCH", magnifier/search icon in the header, six blocks (noise suppressor, P
- `darkglass-anagram-kosmos-04.jpg` — *screen-ui* — Mixer page rendered edge-to-edge and fully legible: X close at left, "Mixer" title, EQ chevron at right, then six channel strips (instrument L/R, aux L/R, headphones, main out) each with icon, dB read
- `darkglass-anagram-kosmos-05.webp` — *screen-ui* — Global EQ page, straight-on and large: X close, centered "Global EQ" title, a row of six band chips (80.0 Hz / 160 Hz / 757 Hz / 2.00 kHz / 5.05 kHz / 8.00 kHz each with a green dB value pill), and th
- `darkglass-anagram-kosmos-06.jpg` — *screen-ui* — Official cropped straight-on shot of the Tuner & Tempo page: title bar, large note readout "F#" centred in a green LED-style strobe ladder with -30/-20/-10/0/10/20/30 cent markings, and a wide "Mute" 
- `darkglass-anagram-kosmos-07.png` — *screen-ui* — Marketplace / block-picker screen at 1920px: "Marketplace" title in the header with a magnifier search affordance, and a horizontally scrolling row of purchasable pedal-block cards (BTD Classic, PunkS
- `darkglass-anagram-kosmos-08.jpg` — *screen-ui* — "Select Tonepack" browser screen: back chevron, TONE3000 logo, NAM / IR filter chips, centered title, favourite (star) and info icons, then result cards with artwork thumbnail, tone title ("Ampeg SVT 
- `darkglass-anagram-kosmos-09.jpg` — *screen-ui* — NAM block detail / loader screen: TONE3000 header with a "Size: Full" selector, quality toggle and an on/off switch, artwork tile with tone title, author and "Experimental NAM" label, a NAM Capture li
- `darkglass-anagram-kosmos-10.jpg` — *device-with-screen* — Straight-on shot of the TONE3000 tone-browser grid on screen: header reads "01A Tone3000" with a search magnifier and the Chain/Preset toggle, and the body is a row of six artwork thumbnail cards each
- `darkglass-anagram-kosmos-11.jpg` — *device-with-screen* — KosmOS v1.16 chain view: preset chip "05A", name "KosmOS 1.16", search magnifier, Chain toggle with the mode chip set to "Stomp" (magenta), and a chain mixing a Peggy Classic amp block, a stacked/para
- `darkglass-anagram-kosmos-12.jpg` — *device-with-screen* — Real-world review photograph (Guitar World) of the chain view in a light-themed skin: "01B Vintage Microtubes*" with the unsaved-edit asterisk and a save icon in the header, Luminal/compressor/Jim Bas

> Method: web-searched official Darkglass pages, the LVGL case study on the Anagram's UI, review articles and the TONE3000 integration blog, then fetched each page's HTML with curl and extracted <img>/CDN URLs directly. Every URL above was downloaded and visually inspected by me, then re-checked with a HEAD request — all 13 return HTTP 200 with an image/* content-type.

## Mobile guitar amp app UIs on phones — editable signal chain references (IK TONEX

- `mobile-guitar-amp-app-uis-on-00.jpg` — *screen-ui* — BIAS FX 2 Mobile chain editor on iPhone (landscape) — the single/dual Routing toggle, FX Add/Replace/Delete/Clear toolbar, and the horizontal signal lane GATE → DIST → EQ → '59 Tweed Lux V2 head → Twe
- `mobile-guitar-amp-app-uis-on-01.jpg` — *screen-ui* — BIAS FX 2 Mobile with the Looper panel open as a height-capped modal card floating over the chain lane — the chain stays visible behind it. Raw 2208x1242 iPhone screenshot. Useful for how an overlay s
- `mobile-guitar-amp-app-uis-on-02.png` — *device-with-screen* — Positive Grid Spark app phone tone editor: a horizontally-scrolling signal-chain strip pinned under the header (…WAH → DRIVE → AMP → MOD/EQ → DEL, each block a pedal/amp thumbnail on a red wire with a
- `mobile-guitar-amp-app-uis-on-03.png` — *device-with-screen* — Spark app 'Tone Engine' App Store panel — upright phone showing the same chain strip (WAH/DRIVE/AMP/MOD-EQ/DEL) above a British 30 amp detail view, preset 'Silver Ship / CH1A' in the header. Second Sp
- `mobile-guitar-amp-app-uis-on-04.png` — *device-with-screen* — IK TONEX Control preset list on iPhone — the strongest 'chain as a list row' reference here. TONEX ONE+/Mobile Device segmented tabs, Dual Mode/Stomp Mode, A/B/C slot selector, then numbered preset ro
- `mobile-guitar-amp-app-uis-on-05.png` — *device-with-screen* — IK TONEX Control 'Advanced Controls' sheet on iPhone — a scrollable, height-capped card over the editor with a dismiss X, knob rows (Gain/Volume, Bass/Mid/Treble, Freq/Freq/Q/Freq), a PRE/POST segment
- `mobile-guitar-amp-app-uis-on-06.png` — *device-with-screen* — IK TONEX Control editor screen on iPhone — preset title '00 A Real Classic' with overflow menu, an amp render, then a block-selector row (power toggle, model name 'Gold Guy', chevron to browse, slider
- `mobile-guitar-amp-app-uis-on-07.jpg` — *device-with-screen* — AmpliTube TONEX on iPhone (landscape) — the VIR virtual-mic page with Mic 1/Mic 2 selectors, Mics Mix and Resonance sliders, an X/Z placement grid on a speaker render, and along the bottom the compact
- `mobile-guitar-amp-app-uis-on-08.jpg` — *device-with-screen* — Neural DSP Cortex Mobile preset page on iPhone — the Quad Cortex grid signal chain rendered as 8 colour-outlined icon blocks in one row between an In 1/2 node and a USB 3/4 out node, with three empty 
- `mobile-guitar-amp-app-uis-on-09.jpg` — *device-with-screen* — Neural DSP Cortex Mobile 'Preset Devices' page — the same chain expressed as a vertical ordered list, one row per block: coloured type icon, small-caps category (Guitar Overdrive, Equalizer, Guitar Am

> All 10 URLs HEAD-checked: HTTP 200 with image/jpeg or image/png. Method was web search → fetch the page → grep the <img>/CDN URLs out of the HTML, then download and visually inspect every candidate before selecting, so each entry above is described from the actual pixels rather than from alt text.
