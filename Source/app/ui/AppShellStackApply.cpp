#include "app/ui/AppShell.h"
#include "app/ui/StackWidgets.h"

// Stack-to-engine apply wiring: makes the open rig's active amp channel +
// cab audible. Split out of AppShellStacks.cpp per the no-god-files rule --
// it was pushing that file past 400 lines. Renamed from AppShellPerform.cpp
// (2026-08-15 Stacks strip): PERFORM/CONTROLS, scenes, and routing are gone
// -- editing IS the only surface now, so this file is purely the apply
// machinery `openStackDetail`/`onChanged`/the item-sheet mutations in
// AppShellStacks.cpp call directly.
//
// Engine truth (Phase A): ONE model + ONE IR. A load is audible only when the
// target toneId differs from what's actually live (tracked here by id, see
// AppShell.h); one nam::ToneInfo load is in flight at a time, a request that
// arrives mid-flight parks in the pending slot matching its resource type
// (model vs IR -- see AppShell.h's pendingModelApply_/pendingIrApply_
// comment), last request wins within each slot, and both drain once the
// in-flight load completes.

namespace {
nam::ToneInfo makeToneInfo (std::string id, std::string title, std::string format) {
    nam::ToneInfo t;
    t.id = std::move (id);
    t.title = std::move (title);
    t.format = std::move (format);
    return t;
}

const juce::String kEmDash = juce::String::fromUTF8 ("\xE2\x80\x94");   // —
}   // namespace

// See AppShell.h's ApplyTimeout comment: one-shot 30s juce::Timer behind the
// arm()/cancel() interface, same shape as AndroidBilling's PurchaseTimeout.
// Message thread only (juce::Timer itself requires it).
struct AppShell::ApplyTimeoutImpl : AppShell::ApplyTimeout, private juce::Timer {
    explicit ApplyTimeoutImpl (AppShell& o) : owner (o) {}
    AppShell& owner;
    int gen = 0;

    void arm (int generation) override {
        gen = generation;
        startTimer (30000);
    }
    void cancel () override { stopTimer (); }

    void timerCallback () override {
        stopTimer ();
        owner.handleApplyTimeout (gen);
    }
};

bool AppShell::findLocalEntry (const nam::ToneInfo& tone, nam::LibraryEntry& out) const {
    if (tone.id.empty ()) return false;
    const bool isIr = (tone.format == "ir");
    if (isIr) {
        if (!getIrs_) return false;
        for (auto& e : getIrs_ ())
            if (e.id == tone.id) {
                out = e;
                return true;
            }
    } else {
        if (!getModels_) return false;
        for (auto& e : getModels_ ())
            if (e.id == tone.id) {
                out = e;
                return true;
            }
    }
    return false;
}

void AppShell::requestToneLoad (int stackIdx, nam::ToneInfo tone, std::function<void ()> onFail) {
    // A local match doesn't need svc_.loadTone at all (see startToneLoad) --
    // only bail here for the lack of BOTH a local entry and a network route.
    nam::LibraryEntry probe;
    if (stackIdx < 0 || stackIdx >= (int)stackList_.size ()) return;
    if (!findLocalEntry (tone, probe) && !svc_.loadTone) return;
    if (performApplyInFlight_) {
        // Keyed by resource type so a model request and an IR request that
        // both arrive mid-flight (applyStackToEngine's model-then-IR pair racing
        // a stack switch) don't collide in one slot -- see AppShell.h.
        auto& slot = (tone.format == "ir") ? pendingIrApply_ : pendingModelApply_;
        slot = { true, stackIdx, juce::String (stackList_[(size_t)stackIdx].uid), std::move (tone),
                 std::move (onFail) };
        return;
    }
    startToneLoad (stackIdx, std::move (tone), std::move (onFail));
}

void AppShell::finishToneLoad (int stackIdx, juce::String stackUid, std::string toneId,
                               std::string title, std::string format, std::function<void ()> onFail,
                               bool ok) {
    performApplyInFlight_ = false;
    // Re-validate: the stack this load was for may have been removed (or
    // the index reused by a different one) while the round trip was in
    // flight. Keyed by uid rather than name -- a rename mid-flight must not
    // be mistaken for the stack having been swapped out. Strict on purpose:
    // onFail/saveStacksState/pushStacks below all index stackList_[stackIdx]
    // directly, so this must confirm THIS EXACT index still holds the stack
    // the load was for -- an unrelated stack removed earlier in the list can
    // shift a DIFFERENT stack onto stackIdx, and a stale onFail must never
    // mutate that one instead.
    const bool stillValid = stackIdx >= 0 && stackIdx < (int)stackList_.size () &&
                            juce::String (stackList_[(size_t)stackIdx].uid) == stackUid;
    // Looser than stillValid on purpose: the failure toast only needs to
    // know the rig is still around to care about, not that it's still at
    // THIS index -- searched by uid so a stack merely shifted by an
    // unrelated removal still gets told its load failed, instead of
    // `stillValid` going false (index now out of range or reused by a
    // different stack) and silently swallowing a genuine failure for a
    // stack that's still very much on screen.
    bool stillExists = stillValid;
    if (!stillExists)
        for (const auto& s : stackList_)
            if (juce::String (s.uid) == stackUid) {
                stillExists = true;
                break;
            }
    if (ok) {
        if (format == "ir") liveIrToneId_ = toneId;
        else liveModelToneId_ = toneId;
    } else {
        if (stillValid && onFail) onFail ();
        // Gated on stillExists (not stillValid): a stack removed mid-load
        // must not toast about a rig that no longer exists anywhere, but a
        // stack that merely moved still deserves to hear its load failed.
        if (stillExists && stacksDetail_ != nullptr)
            nam::ui::showToast (*stacksDetail_, "couldn't load " + juce::String (title) + " " +
                                                    kEmDash + " check connection");
    }
    if (stillValid) {
        saveStacksState ();
        pushStacks ();
    }
    // Drain the parked slots, model first: at most one load starts here
    // (startToneLoad only runs one at a time), and the other -- if also
    // active -- drains on THAT load's completion in turn, so both
    // eventually run even though only one is in flight at once. A slot
    // whose stack vanished while parked is dropped rather than started, in
    // which case the OTHER slot must still get a chance this round --
    // otherwise, with performApplyInFlight_ already false, it would sit
    // orphaned until some unrelated future requestToneLoad happened to
    // flush it.
    if (!drainPendingToneApply (pendingModelApply_)) drainPendingToneApply (pendingIrApply_);
}

bool AppShell::drainPendingToneApply (PendingToneApply& slot) {
    if (!slot.active) return false;
    auto pending = std::move (slot);
    slot = {};
    // Re-validate the parked request the same way finishToneLoad
    // re-validates its own load: the stack it was queued for may have been
    // removed (or its index reused by a different stack) during the load
    // that was in flight while it waited. Without this, an OOB
    // `stackList_[stackIdx]` below is possible (empty vector) or, with >=2
    // stacks, the wrong stack's uids get mutated and persisted on a later
    // failure.
    const bool pendingValid =
        pending.stackIdx >= 0 && pending.stackIdx < (int)stackList_.size () &&
        juce::String (stackList_[(size_t)pending.stackIdx].uid) == pending.stackUid;
    if (!pendingValid) return false;   // dropped -- give the other slot a turn
    startToneLoad (pending.stackIdx, std::move (pending.tone), std::move (pending.onFail));
    return true;   // a load is now in flight again; stop here
}

void AppShell::startToneLoad (int stackIdx, nam::ToneInfo tone, std::function<void ()> onFail) {
    performApplyInFlight_ = true;
    ++performApplyGen_;
    const auto stackUid = juce::String (stackList_[(size_t)stackIdx].uid);
    const auto toneId = tone.id;
    const auto title = tone.title;
    const auto format = tone.format;

    // A chain item built by the now-retired create wizard stores a
    // LibraryEntry filename as toneId, not a TONE3000 id (see decisions.md)
    // -- such items can still exist in an on-disk stacks.json. Route those
    // through the synchronous local model/IR load (instant, offline)
    // instead of handing a filename to svc_.loadTone, which treats every id
    // as a TONE3000 tone id. Resolves the SAME completion/pending-drain
    // machinery (finishToneLoad) a network load would, so the in-flight/
    // pending state doesn't desync. An id that no longer matches (entry
    // deleted from the library) falls through to the network route below,
    // which fails on the bogus id and hits the existing failure toast/
    // revert.
    nam::LibraryEntry local;
    if (findLocalEntry (tone, local)) {
        if (format == "ir") {
            if (loadIr_) loadIr_ (local);
        } else if (loadModel_) loadModel_ (local);
        finishToneLoad (stackIdx, stackUid, toneId, title, format, std::move (onFail), true);
        return;
    }
    if (!svc_.loadTone) {
        finishToneLoad (stackIdx, stackUid, toneId, title, format, std::move (onFail), false);
        return;
    }
    // Real async route: arm the 30s watchdog so a callback that never fires
    // (network hang) can't wedge performApplyInFlight_ -- and with it,
    // editing's audible apply -- forever. inFlightApply_ carries what handleApplyTimeout
    // needs to resolve this exact attempt as a failure through the SAME
    // finishToneLoad path (toast + revert + drain pendings) a real failed
    // callback would take.
    const int gen = performApplyGen_;
    inFlightApply_ = { true, stackIdx, stackUid, tone, onFail };
    if (applyTimeout_ == nullptr) applyTimeout_ = std::make_unique<ApplyTimeoutImpl> (*this);
    applyTimeout_->arm (gen);
    svc_.loadTone (tone, [this, gen, stackIdx, stackUid, toneId, title, format,
                          onFail = std::move (onFail)] (bool ok, juce::String) {
        // Stale: the watchdog (or, after it resolved this attempt as a
        // failure and drained a pending request, that NEW attempt) already
        // settled this slot -- a late real callback must not act on state
        // that's since moved on. Only cancel the timer once confirmed
        // current; it may otherwise belong to that newer attempt.
        if (gen != performApplyGen_) return;
        applyTimeout_->cancel ();
        finishToneLoad (stackIdx, stackUid, toneId, title, format, onFail, ok);
    });
}

void AppShell::handleApplyTimeout (int generation) {
    if (generation != performApplyGen_) return;   // the real callback already resolved this attempt
    auto req = std::move (inFlightApply_);
    inFlightApply_ = {};
    finishToneLoad (req.stackIdx, req.stackUid, req.tone.id, req.tone.title, req.tone.format,
                    std::move (req.onFail), false);
}

// Make the engine match the open rig (active amp channel = model, cab =
// impulse). Idempotent: each half is skipped when that tone is already live.
// Callers are in AppShellStacks.cpp -- including why pushStacks is NOT one.
void AppShell::applyStackToEngine () {
    // VISIBILITY, not stacksShowDetail_: that flag survives navigating to
    // Play, where show() deliberately clears the live ids -- a load
    // completing after the user left would then re-point the engine at the
    // rig, over whatever they just picked on Play.
    if (stacksDetail_ == nullptr || current_ != stacksDetail_.get ()) return;
    const int idx = stacksDetail_->currentIndex ();
    if (idx < 0 || idx >= (int)stackList_.size ()) return;
    const auto& st = stackList_[(size_t)idx];

    const auto* amp = nam::StackModel::activeAmp (st);
    if (amp != nullptr && !amp->channels.empty () && amp->activeChannel >= 0 &&
        amp->activeChannel < (int)amp->channels.size ()) {
        const auto& ch = amp->channels[(size_t)amp->activeChannel];
        if (!ch.toneId.empty () && ch.toneId != liveModelToneId_)
            requestToneLoad (idx, makeToneInfo (ch.toneId, ch.title, "nam"), {});
    }
    const auto* cab = nam::StackModel::cabOf (st);
    if (cab != nullptr && !cab->toneId.empty () && cab->toneId != liveIrToneId_)
        requestToneLoad (idx, makeToneInfo (cab->toneId, cab->title, "ir"), {});
}
