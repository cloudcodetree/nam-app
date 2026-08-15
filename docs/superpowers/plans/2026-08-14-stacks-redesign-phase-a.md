# Stacks Redesign Phase A (UI + Model) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the fixed 6-slot Stacks rig with the new design's ordered-chain model and four surfaces (Home, Detail EDIT, Detail PERFORM on-screen, Create wizard), driven by the existing single-chain engine.

**Architecture:** New JUCE-free `nam::StackModel` (types + v2 JSON + v1 migration, TDD) consumed by four new UI TUs replacing `StacksScreen`. All host plumbing reuses the EXISTING `AppShell::BrowseServices` fields (`loadStacksJson`/`saveStacksJson`/`searchEx`/`loadTone`) — no Android host changes. AppShell's stack wiring moves to a new `AppShellStacks.cpp` TU so `AppShell.cpp` SHRINKS.

**Tech Stack:** C++/JUCE 9.0.1 (UI), nlohmann/json (model, already vendored), Catch2 headless tests, Android emulator E2E.

**Binding docs:** spec `docs/superpowers/specs/2026-08-14-stacks-redesign-phase-a-design.md` (decisions + engine truth); visual reference `docs/superpowers/specs/2026-08-14-stacks-redesign-notes.md` (exact layout/copy/colors per screen — treat its "Stacks screen (new design)" and "Stack creation flow" sections as the pixel spec).

## Global Constraints

- Engine truth unchanged: ONE model + ONE IR. Audible applies go through `svc_.loadTone` (existing `DownloadFn` → host `doLoadToneLive`). Pedals/post are stored + visual only; routing pills render but only SINGLE selectable (others toast "A/B & stereo routing coming soon"); scene tap applies amp channel audibly + bypass map visually.
- New files ≤400 lines each; `AppShell.cpp` (1053) and `AppShellChrome.cpp` (566) are over-cap — they must NOT grow net; stack wiring extracts to `AppShellStacks.cpp`. Every new `.cpp` registered in the Android CMake target (`CMakeLists.txt`, alongside `StacksScreen.cpp`'s current entry, which is removed when the file is deleted); `StackModel.cpp` also in `tests/CMakeLists.txt`.
- TDD for `Source/model` (failing test first, same commit); emulator E2E + screenshots recorded in commit messages for every screen task (build: `cd Builds/Android && JAVA_HOME=/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home ./gradlew assembleDebug`; device emulator-5554; headless suite `cmake --build --preset default --target nam_tests && ./build/tests/nam_tests` stays green).
- Colors ONLY from `nam::ui::col` (digest confirmed zero new hex); non-ASCII glyphs via `juce::String::fromUTF8`; vector `juce::Path` icons; overlays height-capped, scrollable, painted last; hit rects guard empty rects.
- Gating semantics untouched: `kGatesEnabled`/`kSoftPaywall` behavior identical (nav gate + rig-2 creation gate move onto the new Home/create path with the same predicates).
- Exact copy strings from the notes doc are law (e.g. Home subtitle "your rigs — pedals, amps, cabs and post, wired for the floor"; toast "Saved · A–D mapped on your Chocolate"). Em dashes/glyphs via fromUTF8.
- clang-format before every commit; commits auto-push through the adversarial gate (check `.git/autopush.log`); no AI attribution.
- Wiki: dated decisions.md line when the model swap lands (T2) and when PERFORM lands (T4), same commit.

---

### Task 1: nam::StackModel (JUCE-free core, TDD)

**Files:**
- Create: `Source/model/StackModel.h`, `Source/model/StackModel.cpp`
- Test: `tests/test_stack_model.cpp`
- Modify: `CMakeLists.txt` (StackModel.cpp in app targets), `tests/CMakeLists.txt` (both)

**Interfaces (Produces — later tasks depend on these exact names):**
```cpp
namespace nam {
enum class GearType { Pedal, Amp, Cab, Post };
struct StackChannel { std::string toneId, title; };
struct ChainItem {
    std::string uid;            // stable per item, "i1","i2"… assigned by model
    GearType type = GearType::Pedal;
    std::string toneId, title, format;   // format "nam"/"ir" as today
    std::string gearTag;        // original TONE3000 gear ("pedal","amp","cab","outboard","spaces","experimental")
    int fs = 0;                 // 0 = unassigned, 1..8
    bool bypassed = false;
    std::vector<StackChannel> channels;  // amps only; [0] mirrors toneId/title
    int activeChannel = 0;
};
struct Scene { std::string name; std::map<std::string, bool> pedalBypass; int ampChannel = 0; };
struct Stack {
    std::string name;
    enum class Routing { Single, AB, Stereo };
    Routing routing = Routing::Single;
    std::vector<ChainItem> chain;        // ordered, signal top→bottom
    std::vector<Scene> scenes;
    int activeScene = -1;
};
class StackModel {
public:
    static std::vector<Stack> parse (const std::string& json);      // v2, or v1 auto-migrated; malformed → {}
    static std::string serialize (const std::vector<Stack>&);       // always v2: {"version":2,"stacks":[…]}
    static bool canAdd (const Stack&, GearType);                    // false for 2nd Amp/Cab
    static const ChainItem* activeAmp (const Stack&);               // first Amp or nullptr
    static const ChainItem* cabOf (const Stack&);                   // first Cab or nullptr
    static std::string activeModelToneId (const Stack&);            // active channel's toneId, "" if none
    static std::string activeIrToneId (const Stack&);
    struct SceneApply { std::string modelToneId, modelTitle; std::vector<std::pair<std::string,bool>> bypass; };
    static SceneApply sceneApplyPlan (const Stack&, int sceneIdx);  // what a scene tap changes
    static std::string nextUid (const Stack&);                      // "i{max+1}"
};
}
```

- [ ] **Step 1: failing tests** — write `tests/test_stack_model.cpp` FIRST covering: (a) v2 round-trip preserves every field incl. unknown keys on stack and item objects (store extras in an internal `nlohmann::json extra` member merged back on serialize); (b) v1 migration: fixture string in the exact shipped shape — top-level ARRAY of `{"name":…,"slots":[{"id","title","format"}×6]}`, slot order AMP,CABINET,PEDAL,OUTBOARD,SPACES,EXPERIMENTAL — maps to chain order [Amp(slot0, one channel), Cab(slot1), Pedal(slot2), Post(slot3 gearTag "outboard"), Post(slot4 "spaces"), Post(slot5 "experimental")], empty-id slots skipped; (c) `canAdd` amp/cab singleton; (d) `sceneApplyPlan` returns the scene's channel toneId + bypass pairs, clamps bad indices ({} for out-of-range); (e) malformed JSON → empty vector, never throws; (f) `nextUid` monotonic. Run: expect FAIL (no header).
- [ ] **Step 2: implement** `StackModel.h/.cpp` (nlohmann, no JUCE includes) until all pass.
- [ ] **Step 3: full suite green** both presets. Register in both CMake files.
- [ ] **Step 4: commit** `feat: StackModel — ordered-chain stacks core with v1 migration`.

### Task 2: Plumbing swap + Stacks Home

**Files:**
- Create: `Source/app/ui/StacksHomeScreen.h/.cpp`, `Source/app/ui/StackDetailScreen.h/.cpp` (SHELL only this task: header row ‹/name/EDIT|PERFORM pill, empty body), `Source/app/ui/AppShellStacks.cpp`, `Source/app/ui/StackWidgets.h/.cpp` (toast component + routing badge + setlist chip painters)
- Delete: `Source/app/ui/StacksScreen.h/.cpp` (and its CMake entry)
- Modify: `Source/app/ui/AppShell.h` (member/type swap), `Source/app/ui/AppShell.cpp` (MOVE loadStacksState/saveStacksState/pushStacks + stacks wiring OUT to AppShellStacks.cpp — net line count must DROP), `Source/app/ui/AppShellChrome.cpp` (STACKS nav → StacksHomeScreen, same gate predicate), `CMakeLists.txt`

**Interfaces:**
- Consumes: `StackModel` (T1), existing `svc_.loadStacksJson/saveStacksJson`.
- Produces: `AppShell` holds `std::vector<nam::Stack> stackList_; int currentStack_ = 0;` (replaces `StacksScreen::Stack` vector); `AppShellStacks.cpp` implements `AppShell::loadStacksState/saveStacksState/pushStacks/wireStacksScreens()`; `StacksHomeScreen` callbacks: `onCreate()`, `onOpen(int stackIdx)` (→ Detail EDIT), `onPerform(int stackIdx)` (→ Detail PERFORM), `onSetCurrent(int)`, `onSettings()` (→ orb flyout audio settings, same action as today's gear entry point); `StackDetailScreen::setStack(const nam::Stack&, int idx)`, `onBack()`, `onTabChanged(bool perform)`. `StackWidgets`: `nam::ui::showToast(juce::Component& parent, juce::String msg)` (2.2s auto-dismiss, accent border, bottom-anchored), `drawRoutingBadge(g, r, nam::Stack::Routing)`, `drawSetlistChip(g, r, text, active)`.
- Home visuals per notes doc §Home verbatim (brand header, title+pill, subtitle, SETLIST strip, rows with meta "{N} pedals · {N} amp · {N} scenes", routing badge, "▸ PERFORM" pill; empty state = v1 dashed placeholder with new subtitle copy).

- [ ] **Step 1:** StackWidgets TU (toast + painters).
- [ ] **Step 2:** StacksHomeScreen painting + hit-tests (press/drag/tap state machine, chip strip horizontal scroll, list vertical scroll).
- [ ] **Step 3:** Detail shell (back navigates Home; tabs switch an empty body placeholder text).
- [ ] **Step 4:** AppShellStacks.cpp: move state fns; parse via `StackModel::parse` (v1 files migrate transparently — next save writes v2); wire Home/Detail; gate predicates copied verbatim from current mouseDown/onCreate sites (`kGatesEnabled`, `kSoftPaywall`, null-service conventions, rig-2 `canSaveRig` gate on create). Delete StacksScreen.
- [ ] **Step 5:** E2E emulator: existing v1 stacks.json migrates and lists; chips set current; empty state; row→Detail shell; back; PERFORM pill→Detail (PERFORM tab selected, placeholder); create adds "Stack {n}" and persists v2 JSON (verify via `run-as com.namplayer.app cat files/…/stacks.json` or readback log). Screenshots.
- [ ] **Step 6:** `wc -l Source/app/ui/AppShell.cpp` < 1053 (assert in report). Headless suite green. decisions.md line (model swap + v2 format). clang-format, commit `feat: stacks home + ordered-chain model swap (v2 json, v1 migration)`.

### Task 3: Detail EDIT tab (guided + freeform) + picker + item sheet

**Files:**
- Create: `Source/app/ui/StackGearPicker.h/.cpp`, `Source/app/ui/StackItemSheet.h/.cpp`, `Source/app/ui/StackEditView.h/.cpp` (EDIT tab content component hosted by StackDetailScreen)
- Modify: `StackDetailScreen.*` (host EDIT view), `AppShellStacks.cpp` (wiring), `StackWidgets.*` (stomp-card chrome, FS badge, grille strip, cone glyphs — vector Paths), `CMakeLists.txt`

**Interfaces:**
- `StackEditView::setStack(...)`; callbacks `onChanged(nam::Stack)` (owner persists + repushes), `onRemoveStack()`, `onAddGear(GearType slotHint)`, `onOpenItem(juce::String uid)`.
- `StackGearPicker::open(GearType initialTab, bool ampDisabled, bool cabDisabled)`; `onFetch(GearType, std::function<void(bool, std::vector<nam::ToneInfo>)>)` — owner implements via `svc_.searchEx` with gear filters (same params current `onFetchSlotOptions` uses; caption "live from TONE3000 · downloads on add"); `onPicked(GearType, nam::ToneInfo)`. AMP/CAB tabs disabled with hint "one amp per stack for now" when present.
- `StackItemSheet::open(const nam::ChainItem&)`; callbacks `onToggleBypass(uid)`, `onSetFs(uid,int)`, `onSetChannel(uid,int)`, `onAddChannel(uid)` (reopens picker in amp mode targeting channels), `onSwap(uid)`, `onRemove(uid)` (pedal/post only).
- Visuals/copy per notes doc §EDIT (ROUTING pills — SINGLE live, A/B & STEREO tap → toast "A/B & stereo routing coming soon"; FREEFORM toggle lime; PEDALS/AMP/POST sections; pedal-section hint "visual for now — audio support coming"; freeform hint "signal flows top → bottom · reorder anything, anywhere"; ↑/↓ reorder). REMOVE STACK row at EDIT bottom → confirm sheet ("Remove '{name}'?" / REMOVE / CANCEL), removal persists and returns Home.

- [ ] **Step 1:** widgets (stomp card, FS badge, grille, cones) in StackWidgets.
- [ ] **Step 2:** StackEditView guided sections + freeform list + reorder.
- [ ] **Step 3:** picker + item sheet overlays (height-capped, scroll, painted last).
- [ ] **Step 4:** wiring in AppShellStacks.cpp: every mutation → `StackModel` update → `saveStacksState()` → `pushStacks()`; async picker results re-validate stack still exists by index+name (capture uid/idx, check on delivery).
- [ ] **Step 5:** E2E: add pedal (live fetch), add 2nd amp blocked w/ hint, swap amp, add channel, set FS, toggle bypass LED, reorder in freeform, REMOVE STACK; kill+relaunch persists all. Screenshots. Suite green.
- [ ] **Step 6:** clang-format, commit `feat: stack detail EDIT — guided chain, freeform reorder, gear picker, item sheet`.

### Task 4: PERFORM tab (on-screen)

**Files:**
- Create: `Source/app/ui/StackPerformView.h/.cpp`
- Modify: `StackDetailScreen.*` (host), `AppShell.h/.cpp` minimal (nav-visibility flag + back chain), `AppShellChrome.cpp` (skip nav paint/hits when hidden), `AppShellStacks.cpp` (apply wiring), `CMakeLists.txt`, `docs/wiki/decisions.md`

**Interfaces:**
- `StackPerformView::setStack(...)`; callbacks `onSceneTap(int)`, `onStompTap(uid)`, `onAmpCycle()`, `onTuner()`, `onNextStack()/onPrevStack()`, `onExit()`.
- AppShell: `void setNavHidden (bool)` — AppShellChrome paint/mouseDown honor it; `handleBackButton()` order becomes paywall → PERFORM-exit → existing screen chain.
- Apply wiring (AppShellStacks.cpp): scene tap → `StackModel::sceneApplyPlan`; if `modelToneId` differs from the currently-applied one, build `nam::ToneInfo` (id+title+format "nam") → `svc_.loadTone`; on failure toast "couldn't load {title} — check connection" and revert activeScene. Bypass map applies to stored state (visual). One in-flight apply max: taps during flight replace a single pending slot (last tap wins), applied on completion. AMP switch cycles `activeChannel` the same way. Entering PERFORM applies `activeModelToneId`/`activeIrToneId` if not already live. TAP switch: visual BPM readout only (tap intervals averaged over last 4). TUNER opens the existing tuner overlay. Grid, setlist header, SCENES|STOMP toggle, unassigned-stomp toast "Assign in EDIT → tap gear → FOOTSWITCH" per notes doc §PERFORM. Exit chevron hit rect ≥44px. EXP row + MIDI map pill OMITTED (spec).

- [ ] **Step 1:** setNavHidden + back-chain (verify paywall still wins first).
- [ ] **Step 2:** StackPerformView paint/hits.
- [ ] **Step 3:** apply wiring incl. failure toast + last-tap-wins.
- [ ] **Step 4:** E2E: enter PERFORM (nav hides, applies channel — logcat shows load), scene tap audibly swaps (verify via loaded-model title in logcat/UI), stomp LED toggles, unassigned toast, AMP cycles channels, back button exits PERFORM before leaving screen, nav returns. Screenshots + note "audible swap verified via emulator offline-render audition path not required — model load log suffices". Suite green. decisions.md line (PERFORM semantics).
- [ ] **Step 5:** clang-format, commit `feat: stack PERFORM — on-screen scenes/stomp, nav-hidden stage view`.

### Task 5: Create wizard

**Files:**
- Create: `Source/app/ui/StackCreateWizard.h/.cpp` (+ `StackTemplates.h` — the 3 built-ins as constexpr-ish data)
- Modify: `AppShellStacks.cpp` (entry: "+ NEW STACK" → wizard; rig-2 gate unchanged before entry), `CMakeLists.txt`

**Interfaces:**
- `StackCreateWizard::open()`; services injected: `onFetchLibrary(std::function<void(std::vector<nam::LibraryEntry>)>)` (owner uses existing `setLibraryService` getters — models for step 1, IRs for step 3), `onSave(nam::Stack)` (append, persist, navigate Detail EDIT, toast "Saved · A–D mapped on your Chocolate"), `onCancel()`.
- Flow per notes doc §Stack creation: step 0 template gallery (3 templates: Plexi Crunch / Modern Metal / Clean Platform — each a `nam::Stack` literal with empty toneIds but named placeholder items and FS map, so cloning gives structure the user then swaps via EDIT; picking one clones → Detail EDIT immediately; "START EMPTY — BUILD STEP BY STEP" → step 1), steps 1-4 with tappable step pills, footers "NEXT: {STEP} →" / "✓ SAVE STACK" (lime), step-4 Chocolate panel + auto-map (amp-channel cycle + first pedals → A/B/C, D = "Tap tempo") + arm-then-assign + warning "{n} action(s) won't be foot-switchable: {list}". Step 1/3 lists are LOCAL library (models / IRs); step 2 pedal grid also local models tagged pedal — if the library carries no gear tags, list all models with a "from your library" caption and note the limitation in the report.

- [ ] **Step 1:** StackTemplates data + step-0 gallery.
- [ ] **Step 2:** steps 1-4 UI + shared wizard state (gear added in 1-3 appears in 4's action list).
- [ ] **Step 3:** save path + wiring + gate check untouched.
- [ ] **Step 4:** E2E: template → Detail EDIT with cloned structure; empty → 4 steps → save → toast → Detail; FS assignments persist in JSON; rig-2 gate still fires with gates enabled (temporarily set NAM_GATES_ENABLED=1 + kSoftPaywall exercise NOT required — verify with gates off that creation works, and with `NAM_GATES_ENABLED=1` build that the create gate opens the paywall; restore .env after). Screenshots. Suite green.
- [ ] **Step 5:** clang-format, commit `feat: stack creation wizard — templates + 4-step guided flow`.

---

## Out of scope (spec §Out of scope)

MIDI/foot control, dual-amp DSP, audible pedals, Play-screen deltas, template audio content.

## Self-review notes

Spec coverage: model+migration (T1), Home+swap (T2), EDIT+picker+sheet+delete (T3), PERFORM+nav+apply+errors (T4), wizard+templates (T5). Engine-truth rules restated in constraints and T4. Type names checked consistent across tasks (StackModel API of T1 is consumed verbatim in T2-T5; picker/sheet callbacks match T3 wiring; setNavHidden matches T4 usage). No placeholders; copy strings quoted. File-size handling: AppShell must shrink (T2 Step 6 asserts).
