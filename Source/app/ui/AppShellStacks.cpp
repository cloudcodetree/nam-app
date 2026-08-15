#include "app/ui/AppShell.h"
#include <algorithm>
#include <array>
#include "app/ui/StackWidgets.h"
#include "model/Entitlements.h"

// Stacks surface plumbing: JSON persistence (v2 ordered-chain model, via
// StackModel -- v1 fixed-slot files migrate transparently on first load),
// the Home/Detail screen wiring, and the EDIT tab's gear-picker/item-sheet
// mutation logic. Split out of AppShell.cpp per the no-god-files rule;
// AppShell.cpp keeps screen orchestration (show/resized/handleBackButton)
// since those touch state this file doesn't own.

namespace {
// GearType -> TONE3000 gear filter. Cab additionally scopes to format=ir.
// Phase A's picker POST tab fetches "outboard" only -- SPACES/EXPERIMENTAL
// (the other two v1 Post gearTags) are reachable only by swapping an item
// that already carries one of those tags in from a v1-migrated stack, not
// via a fresh live fetch; see decisions.md / the task report.
nam::SearchParams gearSearchParams (nam::GearType t) {
    nam::SearchParams p;
    p.sort = "trending";
    switch (t) {
        case nam::GearType::Amp: p.gears.push_back ("amp"); break;
        case nam::GearType::Cab:
            p.format = "ir";
            p.gears.push_back ("cab");
            break;
        case nam::GearType::Post: p.gears.push_back ("outboard"); break;
        case nam::GearType::Pedal:
        default: p.gears.push_back ("pedal"); break;
    }
    return p;
}
}   // namespace

void AppShell::loadStacksState () {
    stackList_.clear ();
    currentStack_ = 0;
    if (!svc_.loadStacksJson) return;
    const auto raw = svc_.loadStacksJson ();
    const auto rawStd = raw.toStdString ();
    stackList_ = nam::StackModel::parse (rawStd);
    // parse() degrades unreadable content to {} rather than throwing (a
    // corrupt file, or one written by a future app version) -- without a
    // backup, the very next save (any stack edit) would overwrite those raw
    // bytes with an empty rig list, losing them for good. Back the file up
    // once, right here, before that can happen.
    //
    // stackList_.empty() alone isn't enough: a LEGITIMATELY empty, well-
    // formed file (the user deleted their last rig, saving
    // {"version":2,"stacks":[]}) parses to an empty vector too, and isn't
    // corruption -- backing THAT up would overwrite a real recovery .bak
    // (backupStacksJson always overwrites the prior one) with nothing worth
    // recovering. looksLikeStacksFile distinguishes the two: only an
    // unrecognized/corrupt shape triggers the backup.
    if (stackList_.empty () && raw.isNotEmpty () &&
        !nam::StackModel::looksLikeStacksFile (rawStd) && svc_.backupStacksJson)
        svc_.backupStacksJson ();
}

void AppShell::saveStacksState () {
    if (!svc_.saveStacksJson) return;
    // StackModel::serialize always writes v2 -- a v1 file migrated on load
    // is upgraded on the very next save, no explicit migration step needed.
    svc_.saveStacksJson (juce::String (nam::StackModel::serialize (stackList_)));
}

void AppShell::pushStacks () {
    if (stacksHome_ != nullptr) stacksHome_->setStacks (stackList_, currentStack_);
    if (stacksDetail_ != nullptr) {
        // Refresh Detail too when it's showing one of these stacks, so an
        // item-sheet edit (bypass/FS/channel/swap) or a freeform reorder is
        // reflected immediately without re-navigating.
        const int idx = stacksDetail_->currentIndex ();
        if (idx >= 0 && idx < (int)stackList_.size ())
            stacksDetail_->setStack (stackList_[(size_t)idx], idx, (int)stackList_.size ());
    }
    // Deliberately NO applyStackToEngine() here. pushStacks is a RENDER
    // call, and finishToneLoad calls it on the failure path -- making it
    // also trigger a load turned an unloadable tone into an unbounded
    // retry (toast storm, endless HTTP, flash churn; direct recursion when
    // a corrupt cache file fails synchronously). The apply is fired from
    // the specific state transitions that should cause one instead: opening
    // a rig, switching tabs, and each edit that changes the audible truth.
}

void AppShell::openStackDetail (int idx, bool perform) {
    if (idx < 0 || idx >= (int)stackList_.size () || stacksDetail_ == nullptr) return;
    // A stale overlay from whatever stack was previously open must not
    // survive the switch (uid collisions across different stacks are
    // otherwise possible -- "i1" exists in nearly every rig).
    stacksDetail_->picker ().close ();
    stacksDetail_->itemSheet ().close ();
    stacksShowDetail_ = true;
    // With the SETLIST chips gone, "current" means the rig you last opened --
    // CONTROLS' setlist stepping falls back to it when Detail has no index.
    currentStack_ = idx;
    stacksDetail_->setStack (stackList_[(size_t)idx], idx, (int)stackList_.size ());
    // show() BEFORE selectTab, and no applyStackToEngine() call here.
    // selectTab fires onTabChanged, whose handler applies the rig; that
    // apply is guarded on this screen being current_, which only show()
    // makes true -- selecting first would apply while Play was still
    // current_ and be dropped. Letting onTabChanged be the single trigger
    // also keeps it to exactly ONE apply per open: an extra call here would
    // park a duplicate load that the drain later starts against an
    // already-live tone (an audible double-reload every time you open).
    show (Screen::Stacks);
    stacksDetail_->selectTab (perform);
}

void AppShell::openOrbPanel () {
    // Same entry point as tapping the status orb (AppShellChrome.cpp
    // mouseDown): opens the I/O mute/engine flyout. The Stacks gear icon is
    // wired to this rather than duplicating Audio Settings as a second UI.
    ioPanelOpen_ = true;
    ioScrim_.setBounds (contentBounds ());
    ioScrim_.setVisible (true);
    ioScrim_.toFront (false);
    repaint ();
}

void AppShell::wireStacksScreens () {
    stacksHome_->onCreate = [this] {
        // Public-launch config only (kSoftPaywall): the STACKS nav gate lets
        // free users reach Stacks, so creation of a SECOND rig is gated
        // here instead. Policy lives in Entitlements, not this UI layer;
        // isPro_ null stays ungated (desktop convention). Verbatim from the
        // pre-redesign StacksScreen-era onCreate gate.
        if (kGatesEnabled && kSoftPaywall && isPro_) {
            nam::Entitlements ent;
            ent.setPro (isPro_ ());
            if (!ent.canSaveRig ((int)stackList_.size ())) {
                openPaywall (juce::String::fromUTF8 (
                    "Your first rig stays free forever \xE2\x80\x94 Pro adds unlimited rigs"));
                return;
            }
        }
        stacksHome_->wizard ().open ((int)stackList_.size ());
    };
    stacksHome_->onOpen = [this] (int i) { openStackDetail (i, false); };
    stacksHome_->onPerform = [this] (int i) { openStackDetail (i, true); };

    stacksDetail_->onBack = [this] {
        stacksShowDetail_ = false;
        show (Screen::Stacks);
    };
    stacksDetail_->onRemoveStack = [this] {
        const int idx = stacksDetail_->currentIndex ();
        if (idx < 0 || idx >= (int)stackList_.size ()) return;
        stackList_.erase (stackList_.begin () + idx);
        if (currentStack_ >= (int)stackList_.size ())
            currentStack_ = juce::jmax (0, (int)stackList_.size () - 1);
        // Belt and braces alongside startToneLoad's own drain-time
        // revalidation (AppShellPerform.cpp): a pending PERFORM apply
        // parked mid-flight for the stack just removed (or any stack,
        // since every index above `idx` just shifted down) must not
        // resurrect against a since-reused index. Both slots -- a removal
        // can land while either a model or an IR request (or both) is
        // parked.
        pendingModelApply_ = {};
        pendingIrApply_ = {};
        saveStacksState ();
        stacksShowDetail_ = false;
        pushStacks ();
        show (Screen::Stacks);
    };
    stacksDetail_->onChanged = [this] (int idx, nam::Stack st) {
        if (idx < 0 || idx >= (int)stackList_.size ()) return;
        stackList_[(size_t)idx] = std::move (st);
        saveStacksState ();
        pushStacks ();
        applyStackToEngine ();   // a reorder can change which amp is first
    };
    stacksDetail_->onAddGear = [this] (nam::GearType hint) {
        openGearPicker (hint, GearPickerMode::Add, {});
    };

    wireGearPicker ();
    wirePerformView ();    // PERFORM tab: AppShellPerform.cpp
    wireCreateWizard ();   // "+ NEW STACK" wizard, hosted by stacksHome_
}

void AppShell::wireCreateWizard () {
    auto& wiz = stacksHome_->wizard ();
    // Synchronous local-library reads -- same accessors keptModelsSorted()/
    // the cab-choices code already use, no new service plumbing needed.
    wiz.onFetchModels = [this] {
        return getModels_ ? getModels_ () : std::vector<nam::LibraryEntry> ();
    };
    wiz.onFetchIrs = [this] { return getIrs_ ? getIrs_ () : std::vector<nam::LibraryEntry> (); };
    wiz.onSave = [this] (nam::Stack st, juce::String toast) {
        // Minted here (not by the wizard) since uniqueness needs the full
        // stackList_ the wizard doesn't have -- existingStackCount alone
        // could collide with a gap left by an earlier removal (e.g. "s1"
        // deleted, "s3" remains: a count-based mint would reissue "s3").
        // Covers both doSave (guided build) and pickTemplate -- both funnel
        // through this one onSave callback.
        st.uid = nam::StackModel::nextStackUid (stackList_);
        stackList_.push_back (std::move (st));
        currentStack_ = (int)stackList_.size () - 1;
        saveStacksState ();
        pushStacks ();
        openStackDetail (currentStack_, false);
        // The wizard composes the exact toast text itself (spec copy when
        // the FS map is complete, a truthful "{n} action(s) not
        // foot-switchable" variant otherwise) since it's the only place
        // that knows the switch-assignment state; empty means a template
        // pick, which jumps to Detail EDIT silently per spec.
        if (toast.isNotEmpty () && stacksDetail_ != nullptr)
            nam::ui::showToast (*stacksDetail_, toast);
    };
}

void AppShell::openGearPicker (nam::GearType hint, GearPickerMode mode, juce::String targetUid) {
    if (stacksDetail_ == nullptr) return;
    const int idx = stacksDetail_->currentIndex ();
    if (idx < 0 || idx >= (int)stackList_.size ()) return;
    const auto& st = stackList_[(size_t)idx];

    gearPickerMode_ = mode;
    gearPickerTargetUid_ = std::move (targetUid);

    // Which tabs a pick could actually apply to, and why not otherwise --
    // applyGearPick's own early-returns used to be the ONLY enforcement, so
    // a mismatched-tab pick just closed the picker with nothing visibly
    // happening. Disabling the tab (dim + no-op tap, same house pattern
    // Add's singleton cap already used) plus a hint string turns that into
    // visible feedback instead.
    std::array<bool, 4> disabled{ false, false, false, false };
    juce::String hintText;
    if (mode == GearPickerMode::Add) {
        // AddChannel/Swap operate on an EXISTING item, so the one-amp/
        // one-cab-per-stack singleton cap doesn't apply to them -- only a
        // brand-new Add is gated by it.
        const bool ampDisabled = !nam::StackModel::canAdd (st, nam::GearType::Amp);
        const bool cabDisabled = !nam::StackModel::canAdd (st, nam::GearType::Cab);
        disabled[(size_t)nam::GearType::Amp] = ampDisabled;
        disabled[(size_t)nam::GearType::Cab] = cabDisabled;
        // Named per which tab is actually dimmed -- a stack with a cab but
        // no amp only has CAB disabled, and "one amp per stack" would name
        // the wrong limit.
        if (ampDisabled && cabDisabled) hintText = "one amp and one cab per stack for now";
        else if (ampDisabled) hintText = "one amp per stack for now";
        else if (cabDisabled) hintText = "one cab per stack for now";
    } else if (mode == GearPickerMode::AddChannel) {
        // Only an amp tone is a valid channel capture.
        for (int i = 0; i < 4; ++i) disabled[(size_t)i] = (nam::GearType)i != nam::GearType::Amp;
        hintText = "pick an amp capture for this channel";
    } else {   // Swap: must stay the same gear type as the item being replaced
        for (int i = 0; i < 4; ++i) disabled[(size_t)i] = (nam::GearType)i != hint;
        hintText = "swap must stay the same gear type";
    }
    stacksDetail_->picker ().open (hint, disabled, hintText);
}

void AppShell::wireGearPicker () {
    stacksDetail_->picker ().onFetch =
        [this] (nam::GearType type, std::function<void (bool, std::vector<nam::ToneInfo>)> cb) {
            if (!cb) return;
            if (!svc_.searchEx || stacksDetail_ == nullptr) {
                cb (false, {});
                return;
            }
            const int idx = stacksDetail_->currentIndex ();
            if (idx < 0 || idx >= (int)stackList_.size ()) {
                cb (false, {});
                return;
            }
            // Captured for the async reply below -- keyed by uid rather than
            // name so a rename mid-flight isn't mistaken for the stack
            // having been swapped out; also catches the stack having been
            // removed (or the index reused by a different one) while the
            // network round trip was in flight.
            const auto capturedUid = juce::String (stackList_[(size_t)idx].uid);
            svc_.searchEx (gearSearchParams (type),
                           [this, idx, capturedUid, cb] (bool ok, std::vector<nam::ToneInfo> tones,
                                                         juce::String) {
                               if (idx < 0 || idx >= (int)stackList_.size () ||
                                   juce::String (stackList_[(size_t)idx].uid) != capturedUid) {
                                   cb (false, {});
                                   return;
                               }
                               cb (ok, std::move (tones));
                           });
        };
    stacksDetail_->picker ().onPicked = [this] (nam::GearType tab, nam::ToneInfo tone) {
        applyGearPick (tab, std::move (tone));
    };

    auto& sheet = stacksDetail_->itemSheet ();
    sheet.onToggleBypass = [this] (juce::String uid) {
        mutateItem (uid, [] (nam::ChainItem& it) { it.bypassed = !it.bypassed; });
    };
    sheet.onSetFs = [this] (juce::String uid, int fs) {
        mutateItem (uid, [fs] (nam::ChainItem& it) { it.fs = fs; });
    };
    sheet.onSetChannel = [this] (juce::String uid, int ch) {
        mutateItem (uid, [ch] (nam::ChainItem& it) {
            if (ch >= 0 && ch < (int)it.channels.size ()) it.activeChannel = ch;
        });
    };
    sheet.onAddChannel = [this] (juce::String uid) {
        openGearPicker (nam::GearType::Amp, GearPickerMode::AddChannel, uid);
    };
    sheet.onSwap = [this] (juce::String uid) {
        const int idx = stacksDetail_ != nullptr ? stacksDetail_->currentIndex () : -1;
        if (idx < 0 || idx >= (int)stackList_.size ()) return;
        for (const auto& it : stackList_[(size_t)idx].chain)
            if (juce::String (it.uid) == uid) {
                openGearPicker (it.type, GearPickerMode::Swap, uid);
                return;
            }
    };
    sheet.onRemove = [this] (juce::String uid) {
        const int idx = stacksDetail_ != nullptr ? stacksDetail_->currentIndex () : -1;
        if (idx < 0 || idx >= (int)stackList_.size ()) return;
        auto& chain = stackList_[(size_t)idx].chain;
        chain.erase (std::remove_if (
                         chain.begin (), chain.end (),
                         [&] (const nam::ChainItem& it) { return juce::String (it.uid) == uid; }),
                     chain.end ());
        stacksDetail_->itemSheet ().close ();
        saveStacksState ();
        pushStacks ();
        applyStackToEngine ();   // removing the amp/cab changes what's live
    };
}

void AppShell::applyGearPick (nam::GearType tab, nam::ToneInfo tone) {
    if (stacksDetail_ == nullptr) return;
    const int idx = stacksDetail_->currentIndex ();
    if (idx < 0 || idx >= (int)stackList_.size ()) return;
    auto& st = stackList_[(size_t)idx];

    bool changed = false;
    if (gearPickerMode_ == GearPickerMode::AddChannel) {
        if (tab != nam::GearType::Amp) return;   // only an amp tone is a valid channel capture
        for (auto& it : st.chain)
            if (juce::String (it.uid) == gearPickerTargetUid_ && it.type == nam::GearType::Amp) {
                it.channels.push_back ({ tone.id, tone.title });
                changed = true;
                break;
            }
    } else if (gearPickerMode_ == GearPickerMode::Swap) {
        for (auto& it : st.chain)
            if (juce::String (it.uid) == gearPickerTargetUid_) {
                if (it.type != tab) break;   // picker tab must match the item being swapped
                it.toneId = tone.id;
                it.title = tone.title;
                it.format = tone.format;
                it.gearTag = tone.gear;
                if (it.type == nam::GearType::Amp) {
                    it.channels = { { tone.id, tone.title } };
                    it.activeChannel = 0;
                }
                changed = true;
                break;
            }
    } else {                                              // Add: a brand-new chain item
        if (!nam::StackModel::canAdd (st, tab)) return;   // re-check: state may have moved on
        nam::ChainItem it;
        it.uid = nam::StackModel::nextUid (st);
        it.type = tab;
        it.toneId = tone.id;
        it.title = tone.title;
        it.format = tone.format;
        it.gearTag = tone.gear;
        if (tab == nam::GearType::Amp) {
            it.channels.push_back ({ tone.id, tone.title });
            it.activeChannel = 0;
        }
        st.chain.push_back (std::move (it));
        changed = true;
    }
    if (!changed) return;
    saveStacksState ();
    pushStacks ();
    applyStackToEngine ();   // added/swapped gear is audible at once
}

void AppShell::mutateItem (juce::String uid, std::function<void (nam::ChainItem&)> fn) {
    if (!fn || stacksDetail_ == nullptr) return;
    const int idx = stacksDetail_->currentIndex ();
    if (idx < 0 || idx >= (int)stackList_.size ()) return;
    for (auto& it : stackList_[(size_t)idx].chain)
        if (juce::String (it.uid) == uid) {
            fn (it);
            saveStacksState ();
            pushStacks ();
            applyStackToEngine ();   // e.g. an amp channel change
            return;
        }
}
