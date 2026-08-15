#include "app/ui/AppShell.h"
#include "app/ui/NamLookAndFeel.h"
#include <functional>

// Gear-thumbnail plumbing for the Stacks surfaces (Home rig cards, the EDIT
// tab's AMP/CAB/PEDAL/POST cards, and StackGearPicker's result rows). Split
// out of AppShellStacks.cpp/AppShellPerform.cpp (both already at the
// 400-line cap) per the no-god-files rule.
//
// Two id spaces feed this (see AppShellPerform.cpp's findLocalEntry, which
// this reuses): items added via the EDIT picker carry a real TONE3000
// numeric id (svc_.artworkForTone); items added by the create wizard carry a
// LOCAL LibraryEntry filename (artwork_, after matching getModels_/
// getIrs_). Picker rows are always the former -- live search results always
// carry a real id.

namespace {
constexpr int kThumbSide = 160;   // matches AppShellDeck.cpp's thumbOf
constexpr size_t kThumbCacheCap = 128;

juce::Image rescaleThumb (const juce::Image& full) {
    if (!full.isValid ()) return {};
    const float s =
        (float)kThumbSide / (float)juce::jmax (1, juce::jmax (full.getWidth (), full.getHeight ()));
    return full.rescaled (juce::jmax (1, (int)((float)full.getWidth () * s)),
                          juce::jmax (1, (int)((float)full.getHeight () * s)),
                          juce::Graphics::mediumResamplingQuality);
}
}   // namespace

// Same owner-backed juce::Timer shape as AppShellPerform.cpp's
// ApplyTimeoutImpl, parametrized by a callback instead of a fixed owner
// method so Home/EDIT and the picker can each get their own bounded budget
// off one implementation. maxAttempts/intervalMs are per-instance: a picker
// fetch can have 20+ images downloading at once (each a separate thread
// competing for bandwidth), so it gets a longer bounded budget than Home/
// EDIT's handful of items -- still bounded, never polls forever.
struct AppShell::ThumbRetryImpl : AppShell::ThumbRetry, private juce::Timer {
    ThumbRetryImpl (std::function<void ()> onTick, int maxAttempts, int intervalMs)
        : onTick_ (std::move (onTick)), maxAttempts_ (maxAttempts), intervalMs_ (intervalMs) {}
    std::function<void ()> onTick_;
    int attempts = 0;
    const int maxAttempts_;
    const int intervalMs_;

    void ensureRunning () override {
        if (isTimerRunning ()) return;   // already retrying -- don't reset the budget
        attempts = 0;
        startTimer (intervalMs_);
    }
    void stop () override {
        stopTimer ();
        attempts = 0;
    }
    void timerCallback () override {
        // onTick_ FIRST, stopTimer() LAST -- reviewer-caught BLOCKER: the
        // reverse order made this poll forever. onTick_ (pushStackThumbs/
        // pushPickerThumbs) ends by calling ensureRunning() again when still
        // missing; ensureRunning()'s only guard is isTimerRunning(). Calling
        // stopTimer() before onTick_ makes that guard see "not running" on
        // the very tick meant to retire the budget, so ensureRunning() reset
        // attempts to 0 and restarted -- a permanent re-arm every interval,
        // never converging. Stopping AFTER onTick_ keeps isTimerRunning()
        // true for the whole callback, so that same call correctly no-ops,
        // and the stop below is what actually ends it once maxAttempts_ is
        // reached.
        if (onTick_) onTick_ ();
        if (++attempts >= maxAttempts_) stopTimer ();
    }
};

juce::Image AppShell::rescaleAndStoreThumb (const std::string& key, const juce::Image& full) {
    if (!full.isValid ()) return {};
    auto thumb = rescaleThumb (full);
    if (stackThumbCache_.size () > kThumbCacheCap)
        stackThumbCache_.clear ();   // bounded (house rule)
    stackThumbCache_[key] = thumb;
    return thumb;
}

juce::Image AppShell::thumbForTone (const nam::ToneInfo& tone) {
    if (tone.id.empty ()) return {};
    if (auto it = stackThumbCache_.find (tone.id); it != stackThumbCache_.end ()) return it->second;
    return rescaleAndStoreThumb (tone.id,
                                 svc_.artworkForTone ? svc_.artworkForTone (tone) : juce::Image ());
}

juce::Image AppShell::artworkForChainItem (const nam::ChainItem& it,
                                           const std::string& effectiveId) {
    const std::string id = effectiveId.empty () ? it.toneId : effectiveId;
    if (id.empty ()) return {};
    if (auto cached = stackThumbCache_.find (id); cached != stackThumbCache_.end ())
        return cached->second;

    nam::ToneInfo tone;
    tone.id = id;
    tone.title = it.title;
    tone.format = it.format;
    tone.gear = it.gearTag;
    // fetchArtwork (AndroidToneServices.cpp) bails unless imageUrl is a real
    // https URL -- without this, EVERY chain item would report a permanent
    // miss (id.empty() only catches the LOCAL/never-had-one case, not "real
    // id, but the fetch can never start"). "" here for a local/wizard item
    // or a pre-imageUrl file is fine: findLocalEntry below routes those
    // through artwork_ instead, which doesn't need a URL at all.
    tone.imageUrl = it.imageUrl;

    nam::LibraryEntry local;
    const juce::Image full =
        findLocalEntry (tone, local)
            ? (artwork_ ? artwork_ (local) : juce::Image ())
            : (svc_.artworkForTone ? svc_.artworkForTone (tone) : juce::Image ());
    return rescaleAndStoreThumb (id, full);
}

// Home rig cards (active amp, falling back to the cab) + EDIT's AMP/CAB/
// PEDAL/POST cards for every known stack -- one pass covers both consumers
// since both are cheap lookups against chain items already in memory.
void AppShell::pushStackThumbs () {
    std::map<std::string, juce::Image> thumbs;
    bool anyMissing = false;
    for (const auto& st : stackList_)
        for (const auto& it : st.chain) {
            std::string id = it.toneId;
            if (it.type == nam::GearType::Amp && !it.channels.empty () && it.activeChannel >= 0 &&
                it.activeChannel < (int)it.channels.size ())
                id = it.channels[(size_t)it.activeChannel]
                         .toneId;   // AMP card follows the active channel
            if (id.empty () || thumbs.count (id)) continue;
            auto img = artworkForChainItem (it, id);
            if (img.isValid ()) thumbs[id] = img;
            else anyMissing = true;
        }

    if (stacksHome_ != nullptr) stacksHome_->setThumbs (thumbs);
    if (stacksDetail_ != nullptr) stacksDetail_->setThumbs (thumbs);

    if (stackThumbRetry_ == nullptr)
        stackThumbRetry_ =
            std::make_unique<ThumbRetryImpl> ([this] { pushStackThumbs (); }, 4, 700);
    // Gate on visibility (same idea the picker retry applies via
    // isShowing() below) -- without this a rig opened once keeps a
    // background timer ticking (bounded, but pointless) even after the
    // user has moved on to Play. current_ tracks exactly which top-level
    // screen show() last made visible, so this is equivalent to (and
    // cheaper than) stacksHome_/stacksDetail_->isShowing().
    const bool stacksVisible = current_ == stacksHome_.get () || current_ == stacksDetail_.get ();
    if (anyMissing && stacksVisible) stackThumbRetry_->ensureRunning ();
    else stackThumbRetry_->stop ();
}

// StackGearPicker's result rows: called once per fetch completion (open +
// every tab switch, see AppShellStacks.cpp's wireGearPicker), NOT from
// paint -- artworkForTone's fetch-on-miss would otherwise spawn a fresh
// download thread on every repaint while the sheet is visible/scrolling.
void AppShell::wirePickerThumbPush () {
    stacksDetail_->picker ().onResults = [this] (const std::vector<nam::ToneInfo>& tones) {
        pushPickerThumbs (tones);
    };
}

void AppShell::pushPickerThumbs (std::vector<nam::ToneInfo> tones) {
    lastPickerTones_ = tones;
    if (stacksDetail_ == nullptr) return;
    // isShowing(), not isVisible(): picker_ is a child of stacksDetail_ and
    // only close() (a sheet-local tap/pick/dismiss) clears its OWN visible
    // flag -- navigating away from Stacks entirely (nav bar tap while the
    // sheet is open; the nav stays hit-testable over it) hides stacksDetail_
    // itself but never touches the picker's flag, so isVisible() alone
    // would keep reporting "showing" and the retry below would keep ticking
    // for a screen nobody can see (reviewer BLOCKER's visibility gate).
    if (!stacksDetail_->picker ().isShowing ()) {
        if (pickerThumbRetry_ != nullptr) pickerThumbRetry_->stop ();
        return;   // closed before the retry fired -- nothing left to refresh
    }

    std::map<std::string, juce::Image> thumbs;
    bool anyMissing = false;
    for (const auto& t : tones) {
        if (t.id.empty ()) continue;
        auto img = thumbForTone (t);
        if (img.isValid ()) thumbs[t.id] = img;
        else anyMissing = true;
    }
    stacksDetail_->picker ().setThumbs (thumbs);

    // 10x1200ms (~12s bounded) rather than Home/EDIT's 4x700ms: a fetch here
    // can have 20+ concurrent per-image download threads competing for
    // bandwidth, so the short budget mostly expired before enough of them
    // landed (confirmed on device: rows kept resolving well past 7s cold on
    // the emulator's network). Still strictly bounded -- it stops, it just
    // gives a full page of concurrent downloads a realistic window first.
    if (pickerThumbRetry_ == nullptr)
        pickerThumbRetry_ = std::make_unique<ThumbRetryImpl> (
            [this] { pushPickerThumbs (lastPickerTones_); }, 10, 1200);
    if (anyMissing) pickerThumbRetry_->ensureRunning ();
    else pickerThumbRetry_->stop ();
}
