#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <vector>
#include "model/StackModel.h"

// Item detail bottom sheet: type + name, ON/BYPASSED toggle (pedal/post),
// CHANNELS row + "+ capture" (amp), FOOTSWITCH row (NONE, FS1-8), footer
// "SWAP GEAR" (+ "REMOVE" for pedal/post). Same full-parent-bounds /
// self-scrim / height-capped sheet convention as StackGearPicker.
class StackItemSheet : public juce::Component {
public:
    StackItemSheet ();

    void open (const nam::ChainItem&);
    void close ();
    // Which item is currently shown -- lets the owner refresh (or close, if
    // the item was removed) the open sheet after a stack repush without
    // its own separate uid bookkeeping.
    juce::String currentUid () const { return juce::String (item_.uid); }

    std::function<void (juce::String uid)> onToggleBypass;
    std::function<void (juce::String uid, int fs)> onSetFs;   // 0 = NONE
    std::function<void (juce::String uid, int channel)> onSetChannel;
    std::function<void (juce::String uid)> onAddChannel;   // reopens picker in amp mode
    std::function<void (juce::String uid)> onSwap;         // reopens picker in swap mode
    std::function<void (juce::String uid)> onRemove;       // pedal/post only
    std::function<void ()> onDismiss;

    void paint (juce::Graphics&) override;
    void resized () override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void layout ();

    nam::ChainItem item_;

    juce::Rectangle<int> sheetRect_, contentRect_, typeLabelRect_, nameRect_, bypassPillRect_,
        channelsLabelRect_, addChannelRect_, fsLabelRect_, swapBtnRect_, removeBtnRect_;
    std::vector<juce::Rectangle<int>> channelPillRects_;   // content-local within contentRect_
    std::array<juce::Rectangle<int>, 9> fsPillRects_;      // NONE + FS1..FS8, content-local
    int contentH_ = 0;
    float scrollY_ = 0.0f, pressScrollY_ = 0.0f;
    bool pressed_ = false, moved_ = false;
    juce::Point<int> pressPos_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StackItemSheet)
};
