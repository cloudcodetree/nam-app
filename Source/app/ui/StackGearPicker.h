#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
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

    // `ampDisabled`/`cabDisabled`: true when the stack already has one
    // (StackModel::canAdd) -- those tabs render dim with the "one amp per
    // stack for now" hint and reject taps. Re-opening (e.g. tab switch)
    // re-fetches.
    void open (nam::GearType initialTab, bool ampDisabled, bool cabDisabled);
    void close ();

    // Fired on open and on every tab switch; the owner fetches (typically
    // svc_.searchEx) and invokes the callback with the live results.
    // ok=false/{} is a valid "fetch failed or unavailable" reply -- the
    // picker shows an error row, not a crash.
    std::function<void (nam::GearType, std::function<void (bool, std::vector<nam::ToneInfo>)>)>
        onFetch;
    std::function<void (nam::GearType, nam::ToneInfo)> onPicked;
    std::function<void ()> onDismiss;

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

    nam::GearType tab_ = nam::GearType::Pedal;
    bool ampDisabled_ = false, cabDisabled_ = false;
    bool loading_ = false, fetchError_ = false;
    std::vector<nam::ToneInfo> results_;
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
