#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <vector>
#include "dsp/ToneEngine.h"
#include "net/Tone3000Api.h"
#include "model/LibraryEntry.h"
#include "app/ui/PlayScreen.h"
#include "app/ui/AudioSettingsState.h"
#include "app/ui/PaywallPanel.h"
#include "app/ui/StacksHomeScreen.h"
#include "app/ui/StackDetailScreen.h"
#include "app/ui/TunerScreen.h"
#include "model/StackModel.h"

// Soft-paywall strategy for Stacks (Task 8, freemium Pro-unlock). Shared by
// AppShellStacks.cpp (rig-creation gate) and AppShellChrome.cpp (STACKS nav
// gate) so both touchpoints flip together.
//   false (current/internal-testing default): the STACKS nav button itself
//   is hard-gated in AppShellChrome.cpp -- free users never reach the
//   screen, matching the Task 5 config as shipped.
//   true (public-launch config): the nav gate lets everyone in; the first
//   rig is free and AppShellStacks.cpp's wireStacksScreens onCreate gates
//   creation of a SECOND rig instead. Flip this one constant at public launch.
constexpr bool kSoftPaywall = false;

// Master switch for ALL Pro gating (nav/layout/demo/rig gates + lock
// glyphs), driven by the build: set NAM_GATES_ENABLED=0 in .env to develop
// with everything unlocked (CMake injects it; see the .env parse at the
// repo root CMakeLists). Absent or any other value => gates ON, so a store
// build can never ship ungated by omission. The billing wrapper itself
// stays live either way; only the gates consult this.
#ifndef NAM_GATES_ENABLED
#define NAM_GATES_ENABLED 1
#endif
constexpr bool kGatesEnabled = (NAM_GATES_ENABLED != 0);

// Cross-platform app shell: owns every screen and swaps the visible one on
// navigation. Holds the shared dsp::ToneEngine so screens drive it. The
// Android/desktop/iOS shells each just host one AppShell.
class AppShell : public juce::Component {
public:
    explicit AppShell (dsp::ToneEngine& engine);

    void setLevels (float in, float out);            // Play + Audio settings meters
    void setTunerPitch (float hz);                   // 0 = no pitch detected
    void setLatencyMs (double ms);                   // round-trip; shown in the chrome
    void setIoMuted (bool inMuted, bool outMuted);   // reflected on the status orb

    // Status orb mute toggles -> host (true = mute that side).
    using MuteFn = std::function<void (bool)>;
    void setMuteService (MuteFn muteInput, MuteFn muteOutput) {
        muteInput_ = std::move (muteInput);
        muteOutput_ = std::move (muteOutput);
    }
    void showEntryAsNowPlaying (const nam::LibraryEntry& e);           // reflect a loaded tone
    void setNowPlayingInfo (juce::String name, juce::String family);   // non-library tone

    // Android system back: pop to Play if on a sub-screen. Returns true if it
    // handled the press (caller should NOT exit the app); false if already on
    // Play (let the OS do the default = leave the app).
    bool handleBackButton ();

    // Stage-view exception: PERFORM hides the bottom nav entirely (full-bleed
    // switch grid). AppShellChrome.cpp's paint/mouseDown honor this; resized/
    // contentBounds give the reclaimed strip to whichever screen is showing.
    // Idempotent -- safe to call with the value already in effect.
    void setNavHidden (bool hidden);

    // TONE3000 / Browse services, owned by the host. Auditioning renders the
    // demo riff through a model; leaving Browse stops the demo automatically.
    using DoneFn = std::function<void (bool, juce::String)>;
    using SearchFn = std::function<void (
        juce::String, std::function<void (bool, std::vector<nam::ToneInfo>, juce::String)>)>;
    using DownloadFn = std::function<void (nam::ToneInfo, DoneFn)>;
    using AuditionFn = std::function<void (nam::ToneInfo, DoneFn)>;
    struct BrowseServices {
        SearchFn search;
        DownloadFn keep;           // favorite: import the downloaded model
        DownloadFn downloadOnly;   // fetch best quality locally, no play/import
        AuditionFn audition;       // auto variant
        std::function<void (std::string, nam::ModelInfo, bool /*isIr*/, DoneFn)> auditionModel;
        std::function<void (bool)> muteLiveInput;   // Browse mutes the guitar path
        std::function<void (std::string,
                            std::function<void (bool, std::vector<nam::ModelInfo>, juce::String)>)>
            listModels;
        // Async: may need to fetch the DI track first (full TONE3000 catalog).
        std::function<void (int, std::function<void (bool)>)> setDemoTrack;
        std::function<void (int)> setCab;   // cab IR index (0 = none)
        std::function<void ()> stopDemo;
        std::function<bool (std::string)> isAuditionCached;   // toneId, current riff
        std::function<bool (std::string)> isDownloaded;       // best-quality on disk
        std::function<bool (std::string)> isKept;             // in the tone deck
        // Library id of the entry imported for a tone ("" if not kept) —
        // lets Play snap to the deck entry of the last-auditioned tone.
        std::function<std::string (std::string)> libraryIdForTone;
        // Remove a kept entry (model or IR) from the library by its id.
        std::function<void (std::string)> removeKept;
        // Save without hearting (download + import, favorite flag untouched).
        DownloadFn save;
        std::function<bool (std::string)> isSaved;   // on device (library)
        std::function<void (std::string, bool)> setFavoriteById;
        // The hearted deck as browse rows (favorites filter; works offline).
        std::function<std::vector<nam::ToneInfo> ()> listKept;
        // Structured search matching TONE3000's real /tones/search params.
        std::function<void (nam::SearchParams,
                            std::function<void (bool, std::vector<nam::ToneInfo>, juce::String)>)>
            searchEx;
        // Cached/fetch-on-miss artwork for a browse card ({} until fetched).
        std::function<juce::Image (nam::ToneInfo)> artworkForTone;
        // Stacks: persistence + on-the-fly load of a tone into the engine
        // (downloads what it needs; format decides model vs cab impulse).
        std::function<juce::String ()> loadStacksJson;
        std::function<void (juce::String)> saveStacksJson;
        // Safety net for a stacks.json the host can read as bytes but
        // StackModel::parse degrades to {} on (corrupt, or written by a
        // future app version) -- copies the raw file aside (stacks.json.bak)
        // so it survives the very next save, which would otherwise
        // overwrite it with an empty rig list. Called by loadStacksState;
        // the host owns the real file path, AppShell only sees JSON text.
        std::function<void ()> backupStacksJson;
        DownloadFn loadTone;
        std::function<void (bool)> setTestTone;   // orb panel: output check tone
    };
    void setBrowseServices (BrowseServices services);

    // Host reports offline-render progress (0..1) for the in-flight audition.
    void setAuditionProgress (float progress);

    // Library service: list kept models + load one into the engine.
    using GetModelsFn = std::function<std::vector<nam::LibraryEntry> ()>;
    using LoadModelFn = std::function<void (nam::LibraryEntry)>;
    void setLibraryService (GetModelsFn getModels, LoadModelFn loadModel);

    // Host loads cached TONE3000 artwork for a kept tone ({} = none cached).
    using ArtworkFn = std::function<juce::Image (const nam::LibraryEntry&)>;
    void setArtworkService (ArtworkFn artwork) { artwork_ = std::move (artwork); }

    // Kept IR shelf: list entries + load one as the live cab impulse.
    void setIrService (GetModelsFn getIrs, LoadModelFn loadIr) {
        getIrs_ = std::move (getIrs);
        loadIr_ = std::move (loadIr);
        updateCabChoices ();
        pushFilterGroups ();
    }

    // Audio settings service: enumerate devices/rates/buffers, apply picks.
    using GetDevicesFn = std::function<AudioSettingsState ()>;
    using SelectDeviceFn = std::function<void (juce::String)>;
    using RescanFn = std::function<void ()>;
    void setAudioDeviceService (GetDevicesFn get, SelectDeviceFn selectInput,
                                SelectDeviceFn selectOutput, RescanFn rescan = {},
                                SelectDeviceFn selectRate = {}, SelectDeviceFn selectBuffer = {});

    // Pro entitlement: isPro() reads current state; purchase/restore call the
    // store and report back through DoneFn (bool ok, String status). The
    // paywall UI owns disabling BUY/RESTORE while a call is outstanding — a
    // second tap before the first callback lands would drop it.
    void setProServices (std::function<bool ()> isPro, std::function<void (DoneFn)> purchase,
                         std::function<void (DoneFn)> restore);
    // Re-reads isPro() and closes the paywall if it now reports Pro. Called
    // after setProServices wiring fires and whenever the host's billing
    // listener observes a state change (purchase finished, restore landed).
    void refreshProState ();
    // Store's localized price for the Pro product (e.g. "$9.99"), pushed by
    // the host once its getProductsInformation() query returns. Replaces
    // the paywall's static placeholder; safe to call before the paywall has
    // ever been opened (cached and applied on the next/current open).
    void setProPriceText (juce::String price);

    void resized () override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;   // I/O mute panel
    void mouseDown (const juce::MouseEvent&) override;

private:
    enum class Screen { Play, Stacks };
    void show (Screen s);
    void toggleTuner ();     // expand/collapse the tuner overlay
    void runPlayBrowse ();   // structured search from filter state
    void stopAudition ();
    void stepCollection (int delta);               // Play ‹ › through the Library
    juce::Rectangle<int> contentBounds () const;   // above the global chrome

    dsp::ToneEngine& engine_;
    std::unique_ptr<PlayScreen> play_;
    // Stacks surface: both live under Screen::Stacks, one visible at a time
    // (stacksShowDetail_ picks which) -- Detail is a state within Stacks,
    // not its own Screen enumerator (AppShellStacks.cpp).
    std::unique_ptr<StacksHomeScreen> stacksHome_;
    std::unique_ptr<StackDetailScreen> stacksDetail_;
    std::unique_ptr<TunerScreen> tuner_;   // overlay above Play, not a screen
    // Invisible click-catcher under the tuner card: any tap outside the card
    // collapses it (nav taps stay live — the scrim covers content only).
    struct ClickAway : juce::Component {
        std::function<void ()> onTap;
        std::function<void (juce::Point<int>)> onTapAt, onDragAt, onUpAt;   // parent coords
        juce::Point<int> rel (const juce::MouseEvent& e) const {
            return e.getEventRelativeTo (getParentComponent ()).getPosition ();
        }
        void mouseDown (const juce::MouseEvent& e) override {
            if (onTapAt && getParentComponent () != nullptr) onTapAt (rel (e));
            if (onTap) onTap ();
        }
        void mouseDrag (const juce::MouseEvent& e) override {
            if (onDragAt && getParentComponent () != nullptr) onDragAt (rel (e));
        }
        void mouseUp (const juce::MouseEvent& e) override {
            if (onUpAt && getParentComponent () != nullptr) onUpAt (rel (e));
        }
    };
    ClickAway tunerScrim_, ioScrim_;
    void closeIoPanel ();
    void handleIoPanelTap (juce::Point<int> p);
    // I/O device picker inside the orb panel (tap a device name to change
    // it in place; scrollable, height-capped per the overlay rule).
    int ioPicker_ = 0;   // 0 none · 1 input · 2 output · 3 rate · 4 buffer
    juce::StringArray ioPickerItems_;
    juce::String ioPickerCurrent_;
    juce::Rectangle<int> ioPickerRect_;
    float ioPickerScroll_ = 0.0f, ioPickerPressScroll_ = 0.0f;
    int ioPickerContentH_ = 0;
    bool ioPickerPressed_ = false, ioPickerMoved_ = false;
    juce::Point<int> ioPickerPressPos_;
    void openIoPicker (bool output);
    void handleIoDragAt (juce::Point<int> p);
    void handleIoUpAt (juce::Point<int> p);
    bool tunerOpen_ = false;
    int tunerMiss_ = 0;   // consecutive no-pitch feeds (panel hold)
    juce::Component* current_ = nullptr;

    BrowseServices svc_;
    std::string auditionToneId_;   // engine still runs the last-auditioned tone
    int auditioningPack_ = -1, auditioningModel_ = -1;
    int collectionIndex_ = -1;   // Play screen position in the Library

    // Global bottom chrome: persistent nav bar with a central status orb —
    // circular meter (input arc left / output arc right), latency readout in
    // the centre, tap opens the I/O mute panel.
    // Nav: BROWSE / FAVORITES | orb | STACKS / ⋯ (more menu holds DOWNLOADED).
    juce::Rectangle<int> navBar_, orbRect_, navBrowseRect_, navFavRect_, navStacksRect_,
        navMoreRect_;
    void setDeckMode (int mode);   // nav deck buttons (0 fav · 1 saved · 2 browse)
    // ⋯ menu: small overlay above the nav's right corner (house overlay style).
    bool moreOpen_ = false;
    juce::Rectangle<int> moreRect_;
    juce::Rectangle<int> moreDownloadedRect_;
    ClickAway moreScrim_;
    void openMoreMenu ();
    void closeMoreMenu ();
    juce::Rectangle<int> ioPanelRect_, ioEngRow_, ioInRow_, ioOutRow_;
    juce::Rectangle<int> ioRatePill_, ioBufPill_;   // ENGINE row dropdowns
    void openEnginePicker (bool buffer);            // rate / buffer chooser
    juce::Rectangle<int> ioTestRect_;               // TEST TONE toggle row
    bool testToneOn_ = false;
    bool ioPanelOpen_ = false;
    float meterInPeak_ = 0.0f, meterOutPeak_ = 0.0f;
    bool inMuted_ = false, outMuted_ = false;
    MuteFn muteInput_, muteOutput_;
    double latencyMs_ = 0.0;
    GetModelsFn getModels_, getIrs_;
    LoadModelFn loadModel_, loadIr_;
    ArtworkFn artwork_;
    // Play deck state: favorites (library models + kept IRs) or TONE3000
    // browse results, swiped as cards.
    int deckMode_ = 0;                      // 0 favorites · 1 downloaded (all saved) · 2 browse
    std::vector<nam::ToneInfo> playDeck_;   // browse view items
    int playDeckIndex_ = -1;
    std::vector<nam::LibraryEntry> favDeckAll () const;   // models + kept IRs
    std::vector<nam::LibraryEntry> favDeck () const;      // ... filtered
    void pushFilterGroups ();
    int favGear_ = -1;   // -1 all · 0 amps · 1 cabs
    juce::StringArray favTags_, favMakes_;
    int browseGear_ = 0;             // gear dropdown index (0 = all)
    int browsePage_ = 1;             // last TONE3000 page fetched (1-based)
    bool browseFetching_ = false;    // an append fetch is in flight
    bool browseExhausted_ = false;   // a short page came back: no more results
    int browseGen_ = 0;              // bumped per fresh query; stale fetches drop
    bool browseLoaded_ = false;      // page 1 of the current query has landed
    void fetchMoreBrowse ();         // append the next page (infinite scroll)
    nam::SearchParams buildBrowseParams () const;
    static constexpr int kBrowseDeckCap = 300;   // bounded (house rule)
    std::vector<PlayScreen::FilterGroup> browseGroups_;
    void showFavCard (int index, bool loadIntoEngine);
    void showBrowseCard (int index);
    void pushDeckItems ();                            // list/grid rows (AppShellDeck.cpp)
    std::map<std::string, juce::Image> thumbCache_;   // id -> small image (bounded)
    void updateCabChoices ();
    // Stacks state (persisted through the host as JSON, v2 ordered-chain
    // model with transparent v1 fixed-slot migration -- see StackModel).
    // Persistence + Home/Detail wiring live in AppShellStacks.cpp; screen
    // orchestration (show/resized/handleBackButton) stays here.
    std::vector<nam::Stack> stackList_;
    int currentStack_ = 0;            // SETLIST "current" chip (session-only)
    bool stacksShowDetail_ = false;   // which child is visible under Screen::Stacks
    void loadStacksState ();
    void saveStacksState ();
    void pushStacks ();
    void wireStacksScreens ();   // Home/Detail callbacks
    void wireCreateWizard ();    // "+ NEW STACK" wizard callbacks (AppShellStacks.cpp)
    void openStackDetail (int idx, bool perform);
    // What a StackGearPicker pick means: a brand-new chain item (canAdd
    // gated), a new channel appended to an existing amp item, or a
    // wholesale replacement of an existing item's gear. Set whenever
    // AppShellStacks.cpp opens the picker; read back once onPicked fires.
    enum class GearPickerMode { Add, AddChannel, Swap };
    GearPickerMode gearPickerMode_ = GearPickerMode::Add;
    juce::String gearPickerTargetUid_;   // AddChannel/Swap target; empty for Add
    void openGearPicker (nam::GearType hint, GearPickerMode, juce::String targetUid);
    void wireGearPicker ();   // picker()/itemSheet() overlay callbacks
    void applyGearPick (nam::GearType tab, nam::ToneInfo tone);   // picker()'s onPicked
    // Finds the open Detail stack's chain item by uid, runs `fn` on it,
    // then persists + repushes -- the shared tail of every item-sheet edit
    // (bypass/fs/channel). No-ops if the stack or item no longer exists.
    void mutateItem (juce::String uid, std::function<void (nam::ChainItem&)> fn);
    void openOrbPanel ();   // gear icon on Stacks -> same entry point as the orb tap

    // PERFORM tab apply wiring (AppShellStacks.cpp). Engine truth: ONE model
    // + ONE IR; a scene/AMP tap is audible only when the target toneId
    // differs from what's actually live (tracked here by id, not assumed
    // from stored state). Play-side loads (loadModel_/loadIr_/setCab) don't
    // update these ids directly -- AppShell::show() clears both whenever
    // Play becomes the nav target instead (every Play-side load requires
    // Play visible), so a PERFORM re-entry after a Play-side swap always
    // re-validates against the engine rather than trusting a stale id.
    // Worst case is one redundant reload, which is harmless. Bypass +
    // activeScene/activeChannel are STORED-state writes (visual LEDs only
    // in Phase A -- no multi-pedal DSP chain exists yet) and always apply
    // immediately, independent of whether the audible load below them
    // succeeds.
    std::string liveModelToneId_, liveIrToneId_;
    void wirePerformView ();                        // performView() callbacks (AppShellPerform.cpp)
    void enterPerform ();                           // apply activeModel/IrToneId if not live
    void applyScene (int stackIdx, int sceneIdx);   // SCENES switch tap
    void applyAmpCycle (int stackIdx);              // AMP switch tap
    void stepPerformStack (int delta);              // setlist ‹ › / NEXT ▸ switch

    // One in-flight nam::ToneInfo load at a time; a request that arrives
    // mid-flight parks in the slot matching its resource type (model vs IR,
    // by tone.format) rather than starting a second concurrent
    // svc_.loadTone call. Two slots, not one -- enterPerform issues a model
    // load then an IR load back to back, so a NEXT/‹ › switch or a scene tap
    // that lands while the first is still in flight must not let the IR
    // request overwrite the parked model request (or vice versa). Each slot
    // is still last-tap-wins for its own resource; startToneLoad's
    // completion drains both (one at a time -- draining the second happens
    // on the next completion, since only one load runs at once).
    bool performApplyInFlight_ = false;
    struct PendingToneApply {
        bool active = false;
        int stackIdx = -1;
        // Revalidated against by uid (not name -- a rename must not be
        // mistaken for a different stack, and a uid is stable across a
        // freeform reorder or edit the way an index alone is not), same
        // pattern as wireGearPicker's onFetch.
        juce::String stackUid;
        nam::ToneInfo tone;
        std::function<void ()> onFail;
    };
    PendingToneApply pendingModelApply_, pendingIrApply_;
    void requestToneLoad (int stackIdx, nam::ToneInfo tone, std::function<void ()> onFail);
    void startToneLoad (int stackIdx, nam::ToneInfo tone, std::function<void ()> onFail);
    // Wizard-built chain items store a LibraryEntry id (filename) in
    // ChainItem::toneId, not a TONE3000 tone id -- see decisions.md. Looks
    // tone.id up against the local library (models via getModels_, IRs via
    // getIrs_, picked by tone.format) so startToneLoad can route those
    // loads synchronously/offline instead of handing a filename to
    // svc_.loadTone. False (out untouched) if no getter is wired or no
    // entry matches.
    bool findLocalEntry (const nam::ToneInfo& tone, nam::LibraryEntry& out) const;
    // startToneLoad's completion: shared by the local (synchronous) and
    // network (svc_.loadTone callback) routes so both resolve the SAME
    // live-id/persist/pending-drain machinery. Pulled out of startToneLoad
    // as its own named function (was an inline lambda) to keep startToneLoad
    // itself under the ~60-line function-size rule.
    void finishToneLoad (int stackIdx, juce::String stackUid, std::string toneId, std::string title,
                         std::string format, std::function<void ()> onFail, bool ok);
    // Starts the next parked request in `slot`, if any and still valid (the
    // stack it targeted may have vanished while parked). Returns true if a
    // load was started (the OTHER slot gets no turn this round -- it drains
    // on that load's own completion). Called only from finishToneLoad.
    bool drainPendingToneApply (PendingToneApply& slot);

    bool navHidden_ = false;   // PERFORM stage view; see setNavHidden
    void pushPairChoices ();   // PAIR row matches the card's gear
    std::vector<nam::LibraryEntry> keptModelsSorted () const;
    int cabBuiltinCount_ = 0;   // names beyond this are kept IRs
    juce::StringArray cabChoiceNames_;
    bool curCardIsCab_ = false;
    int pairCabSel_ = 0;   // amp cards: current cab pick
    GetDevicesFn getDevices_;
    SelectDeviceFn selectInput_, selectOutput_, selectRate_, selectBuffer_;
    RescanFn rescanDevices_;

    // Pro unlock: paywall overlay + the host's billing services.
    void openPaywall (const juce::String& reason);
    std::unique_ptr<PaywallPanel> paywall_;
    std::function<bool ()> isPro_;
    std::function<void (DoneFn)> purchasePro_, restorePro_;
    // True from the moment BUY/RESTORE is tapped until its DoneFn fires —
    // survives the sheet being dismissed and reopened, so a re-opened
    // paywall shows the correct busy state instead of re-arming the buttons
    // over an unresolved store call.
    bool proCallInFlight_ = false;
    // Store-localized price ("UNLOCK · $9.99" until the host's product-info
    // query lands), cached so a paywall opened before the query resolves
    // still gets the real price once it arrives.
    juce::String proPriceText_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AppShell)
};
