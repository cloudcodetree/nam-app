#include "app/ui/AppShell.h"
#include "app/ui/StackWidgets.h"

// PERFORM tab apply wiring: wirePerformView() connects StackPerformView's
// callbacks (AppShellStacks.cpp owns wireStacksScreens() itself and just
// calls this), plus the engine-apply logic those callbacks and
// AppShell::show()'s nav-hide hook drive. Split out of AppShellStacks.cpp
// per the no-god-files rule -- it was pushing that file past 400 lines.
//
// Engine truth (Phase A): ONE model + ONE IR. A tap is audible only when the
// target toneId differs from what's actually live (tracked here by id, see
// AppShell.h); one nam::ToneInfo load is in flight at a time, a tap that
// arrives mid-flight replaces the single pending slot (last tap wins).

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

void AppShell::wirePerformView () {
    stacksDetail_->onTabChanged = [this] (bool perform) {
        setNavHidden (perform);
        if (perform) enterPerform ();
    };

    auto& perf = stacksDetail_->performView ();
    perf.onExit = [this] { stacksDetail_->selectTab (false); };
    perf.onSceneTap = [this] (int sceneIdx) {
        applyScene (stacksDetail_->currentIndex (), sceneIdx);
    };
    // Stomp bypass is STORED state only (Phase A has no multi-pedal DSP
    // chain -- see decisions.md), so this is the same plain mutateItem
    // wireGearPicker's onToggleBypass already uses; the unassigned-slot
    // toast is handled entirely inside the view, this never fires for it.
    perf.onStompTap = [this] (juce::String uid) {
        mutateItem (uid, [] (nam::ChainItem& it) { it.bypassed = !it.bypassed; });
    };
    perf.onAmpCycle = [this] { applyAmpCycle (stacksDetail_->currentIndex ()); };
    perf.onTuner = [this] { toggleTuner (); };
    perf.onNextStack = [this] { stepPerformStack (+1); };
    perf.onPrevStack = [this] { stepPerformStack (-1); };
}

void AppShell::requestToneLoad (int stackIdx, nam::ToneInfo tone, std::function<void ()> onFail) {
    if (stackIdx < 0 || stackIdx >= (int)stackList_.size () || !svc_.loadTone) return;
    if (performApplyInFlight_) {
        pendingPerformApply_ = { true, stackIdx, juce::String (stackList_[(size_t)stackIdx].name),
                                 std::move (tone), std::move (onFail) };
        return;
    }
    startToneLoad (stackIdx, std::move (tone), std::move (onFail));
}

void AppShell::startToneLoad (int stackIdx, nam::ToneInfo tone, std::function<void ()> onFail) {
    performApplyInFlight_ = true;
    const auto stackName = juce::String (stackList_[(size_t)stackIdx].name);
    const auto toneId = tone.id;
    const auto title = tone.title;
    const auto format = tone.format;
    svc_.loadTone (
        tone, [this, stackIdx, stackName, toneId, title, format, onFail] (bool ok, juce::String) {
            performApplyInFlight_ = false;
            // Re-validate: the stack this load was for may have been
            // removed (or the index reused by a different one) while the
            // round trip was in flight.
            const bool stillValid = stackIdx >= 0 && stackIdx < (int)stackList_.size () &&
                                    juce::String (stackList_[(size_t)stackIdx].name) == stackName;
            if (ok) {
                if (format == "ir") liveIrToneId_ = toneId;
                else liveModelToneId_ = toneId;
            } else {
                if (stillValid && onFail) onFail ();
                if (stacksDetail_ != nullptr)
                    nam::ui::showToast (*stacksDetail_, "couldn't load " + juce::String (title) +
                                                            " " + kEmDash + " check connection");
            }
            if (stillValid) {
                saveStacksState ();
                pushStacks ();
            }
            if (pendingPerformApply_.active) {
                auto pending = std::move (pendingPerformApply_);
                pendingPerformApply_ = {};
                // Re-validate the parked request the same way the completion
                // above re-validates its own load: the stack it was queued for
                // may have been removed (or its index reused by a different
                // stack) during the load that was in flight while it waited.
                // Without this, an OOB `stackList_[stackIdx]` below is possible
                // (empty vector) or, with >=2 stacks, the wrong stack's uids get
                // mutated and persisted on a later failure.
                const bool pendingValid =
                    pending.stackIdx >= 0 && pending.stackIdx < (int)stackList_.size () &&
                    juce::String (stackList_[(size_t)pending.stackIdx].name) == pending.stackName;
                if (pendingValid)
                    startToneLoad (pending.stackIdx, std::move (pending.tone),
                                   std::move (pending.onFail));
            }
        });
}

void AppShell::enterPerform () {
    if (stacksDetail_ == nullptr) return;
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

void AppShell::applyScene (int stackIdx, int sceneIdx) {
    if (stackIdx < 0 || stackIdx >= (int)stackList_.size ()) return;
    auto& st = stackList_[(size_t)stackIdx];
    if (sceneIdx < 0 || sceneIdx >= (int)st.scenes.size ()) return;

    const auto plan = nam::StackModel::sceneApplyPlan (st, sceneIdx);
    const int prevScene = st.activeScene;
    std::string ampUid;
    int prevChannel = 0;
    for (const auto& it : st.chain)
        if (it.type == nam::GearType::Amp) {
            ampUid = it.uid;
            prevChannel = it.activeChannel;
            break;
        }

    // STORED state (bypass map, activeScene, the amp's activeChannel index)
    // applies immediately regardless of the audible load below -- Phase A's
    // bypass is visual-only (no multi-pedal DSP chain exists), and the
    // channel index mirrors what the scene calls for even before the load
    // confirms it landed (reverted together with activeScene on failure).
    st.activeScene = sceneIdx;
    for (const auto& kv : plan.bypass)
        for (auto& it : st.chain)
            if (it.uid == kv.first) it.bypassed = kv.second;
    if (!ampUid.empty ()) {
        const auto& scene = st.scenes[(size_t)sceneIdx];
        for (auto& it : st.chain)
            if (it.uid == ampUid && !it.channels.empty ()) {
                it.activeChannel =
                    (scene.ampChannel >= 0 && scene.ampChannel < (int)it.channels.size ())
                        ? scene.ampChannel
                        : 0;
                break;
            }
    }
    saveStacksState ();
    pushStacks ();

    if (plan.modelToneId.empty () || plan.modelToneId == liveModelToneId_) return;
    requestToneLoad (stackIdx, makeToneInfo (plan.modelToneId, plan.modelTitle, "nam"),
                     [this, stackIdx, prevScene, ampUid, prevChannel] {
                         if (stackIdx < 0 || stackIdx >= (int)stackList_.size ()) return;
                         auto& s = stackList_[(size_t)stackIdx];
                         s.activeScene = prevScene;
                         if (!ampUid.empty ())
                             for (auto& it : s.chain)
                                 if (it.uid == ampUid) {
                                     // The chain may have been edited (swap/remove
                                     // channel) via EDIT while this load was in flight --
                                     // clamp rather than persist an OOB activeChannel.
                                     if (prevChannel >= 0 && prevChannel < (int)it.channels.size ())
                                         it.activeChannel = prevChannel;
                                     break;
                                 }
                     });
}

void AppShell::applyAmpCycle (int stackIdx) {
    if (stackIdx < 0 || stackIdx >= (int)stackList_.size ()) return;
    auto& st = stackList_[(size_t)stackIdx];
    for (auto& it : st.chain) {
        if (it.type != nam::GearType::Amp) continue;
        if (it.channels.empty ()) return;
        const std::string ampUid = it.uid;
        const int prevChannel = it.activeChannel;
        const int newChannel = (prevChannel + 1) % (int)it.channels.size ();
        it.activeChannel = newChannel;
        const auto toneId = it.channels[(size_t)newChannel].toneId;
        const auto title = it.channels[(size_t)newChannel].title;
        saveStacksState ();
        pushStacks ();

        if (toneId.empty () || toneId == liveModelToneId_) return;
        requestToneLoad (stackIdx, makeToneInfo (toneId, title, "nam"),
                         [this, stackIdx, ampUid, prevChannel] {
                             if (stackIdx < 0 || stackIdx >= (int)stackList_.size ()) return;
                             auto& s = stackList_[(size_t)stackIdx];
                             for (auto& jt : s.chain)
                                 if (jt.uid == ampUid) {
                                     // Same clamp as applyScene's revert --
                                     // the channel list may have been
                                     // edited via EDIT while this load was
                                     // in flight.
                                     if (prevChannel >= 0 && prevChannel < (int)jt.channels.size ())
                                         jt.activeChannel = prevChannel;
                                     break;
                                 }
                         });
        return;
    }
}

void AppShell::stepPerformStack (int delta) {
    if (stackList_.empty ()) return;
    const int n = (int)stackList_.size ();
    const int cur = stacksDetail_ != nullptr ? stacksDetail_->currentIndex () : currentStack_;
    const int next = ((cur + delta) % n + n) % n;
    currentStack_ = next;
    openStackDetail (next, true);   // re-applies via onTabChanged -> enterPerform
}
