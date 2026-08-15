#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "model/StackModel.h"
#include "net/Tone3000Api.h"

// Shared "add/swap gear" overlay: type tabs PEDAL/AMP/CAB/POST, one live
// TONE3000 fetch per tab (owner-supplied via onFetch), scrollable result
// rows (title + format tag). Serves both the EDIT tab's "+ ADD" actions and
// the item-detail sheet's "SWAP GEAR". Sizes itself to its parent's full
// bounds while open (so it is the frontmost hit target and an outside tap
// dismisses it) but paints its own content into a height-capped,
// bottom-anchored, scrollable sheet rect -- the house overlay rule.
class StackGearPicker : public juce::Component {
public:
    StackGearPicker ();

    // `disabledTabs` (indexed by nam::GearType) render dim and reject taps
    // -- covers three distinct reasons a tab can be off-limits: Add mode's
    // one-amp/one-cab-per-stack singleton cap (StackModel::canAdd), Add
    // Channel needing an amp tone specifically, and Swap needing the SAME
    // gear type as the item being replaced. `hint`, shown under the caption
    // when any tab is disabled, explains why (e.g. "one amp per stack for
    // now"); empty = no disabled tabs, no hint row. Re-opening (e.g. tab
    // switch) re-fetches. `initialTab` is expected to already be an enabled
    // one (every call site opens on the tab it knows is valid); if it
    // isn't, this falls back to the first enabled tab instead.
    void open (nam::GearType initialTab, std::array<bool, 4> disabledTabs, juce::String hint);
    void close ();
    // Row-thumbnail lookup, keyed by toneId -- pushed by the owner once per
    // fetch completion (AppShellStackThumbs.cpp's pushPickerThumbs), not
    // pulled here: every result row is always a real TONE3000 id, so this
    // never needs the two-id-space routing the Stacks screens do.
    void setThumbs (std::map<std::string, juce::Image> thumbs);

    // Fired on open and on every tab switch; the owner fetches (typically
    // svc_.searchEx) and invokes the callback with the live results.
    // ok=false/{} is a valid "fetch failed or unavailable" reply -- the
    // picker shows an error row, not a crash.
    std::function<void (nam::GearType, std::function<void (bool, std::vector<nam::ToneInfo>)>)>
        onFetch;
    std::function<void (nam::GearType, nam::ToneInfo)> onPicked;
    std::function<void ()> onDismiss;
    // Fired only when a fetch reply is actually applied to results_ (i.e.
    // it survived the fetchGen_/tab_ staleness check below) -- lets the
    // owner push a matching thumbnail map without redoing that same check
    // itself. Reviewer MAJOR: the owner used to push thumbs straight off
    // its own onFetch reply, which only guarded "is this still the same
    // STACK" -- a reply for a tab the user has since switched away from
    // (PEDAL fetch lands after the user taps AMP) still passed that guard
    // and stomped the newer tab's thumbnail map with the old tab's ids.
    std::function<void (const std::vector<nam::ToneInfo>&)> onResults;

    void paint (juce::Graphics&) override;
    void resized () override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void layout ();
    void fetchTab ();
    void selectTab (nam::GearType);
    bool tabDisabled (nam::GearType) const;
    juce::Image thumbFor (const nam::ToneInfo&) const;

    nam::GearType tab_ = nam::GearType::Pedal;
    std::array<bool, 4> disabledTabs_{};
    juce::String hint_;
    bool loading_ = false, fetchError_ = false;
    std::vector<nam::ToneInfo> results_;
    std::map<std::string, juce::Image> thumbs_;
    int fetchGen_ = 0;   // bumped per fetchTab(); a stale async reply is dropped

    juce::Rectangle<int> sheetRect_, tabsRect_, captionRect_, hintRect_, listRect_;
    std::array<juce::Rectangle<int>, 4> tabRects_;
    std::vector<juce::Rectangle<int>> rowRects_;   // content-local, listRect_ origin
    int contentH_ = 0;
    float scrollY_ = 0.0f, pressScrollY_ = 0.0f;
    bool pressed_ = false, moved_ = false;
    juce::Point<int> pressPos_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StackGearPicker)
};
