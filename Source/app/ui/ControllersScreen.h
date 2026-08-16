#pragma once
#include <deque>
#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/ControlMap.h"

// Foot-controller setup: pair a BLE pedal, watch what it sends, and bind its
// switches to actions. One functional header row (back chevron + title), a
// scrolling body, and no chrome of its own -- the bottom nav stays owned by
// AppShell (CLAUDE.md chrome grammar).
//
// The live monitor is not a debug affordance: it is how a user (and the wiki
// verification procedure) discovers what a pedal actually sends, since the
// Chocolate's factory CC assignments are contested and user-editable. See
// docs/wiki/chocolate-plus.md.
//
// NOT Pro-gated (Chris, 2026-08-16): controller support is app-native and
// could legitimately be gated, but it ships free.
class ControllersScreen : public juce::Component {
public:
    ControllersScreen ();

    // --- state pushed by the owner (AppShellControls.cpp) ----------------
    void setDevices (std::vector<juce::String> names);
    void setBindings (std::vector<nam::ControlBinding> bindings);
    void setLearning (nam::ControlAction);   // None = not learning
    // Appends to the live monitor (newest first, capped).
    void pushEvent (const nam::ControlEvent&);
    // True when the platform can pair BLE MIDI at all; hides the button when
    // it cannot (desktop) rather than offering a dead control.
    void setPairingAvailable (bool);

    std::function<void ()> onBack;
    std::function<void ()> onPair;
    std::function<void ()> onRescan;
    std::function<void (nam::ControlAction)> onLearn;
    std::function<void (nam::ControlAction)> onClear;
    // Cycles Auto -> Momentary -> Toggle. Required, not optional: Auto
    // double-fires a momentary switch held past its window, and this override
    // is the only cure (see decisions.md / controllers.md).
    std::function<void (nam::ControlAction)> onCyclePolicy;

    void paint (juce::Graphics&) override;
    void resized () override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void layout ();
    void handleContentTap (juce::Point<int> contentLocal);
    void paintDeviceCard (juce::Graphics&, juce::Rectangle<int>) const;
    void paintMonitorCard (juce::Graphics&, juce::Rectangle<int>) const;
    void paintBindingRow (juce::Graphics&, const juce::Rectangle<int>&, nam::ControlAction) const;

    const nam::ControlBinding* bindingFor (nam::ControlAction) const;

    std::vector<juce::String> devices_;
    std::vector<nam::ControlBinding> bindings_;
    std::deque<juce::String> monitor_;
    nam::ControlAction learning_ = nam::ControlAction::None;
    bool pairingAvailable_ = false;

    juce::Rectangle<int> backRect_, titleRect_, contentArea_;
    int contentH_ = 0;
    float scrollY_ = 0.0f, pressScrollY_ = 0.0f;
    bool dragging_ = false;

    // Content-local rects (0,0 == contentArea_'s top-left).
    juce::Rectangle<int> pairRect_, rescanRect_;
    struct RowRect {
        juce::Rectangle<int> body, learnBtn, policyBtn, clearBtn;
        nam::ControlAction action = nam::ControlAction::None;
    };
    std::vector<RowRect> rowRects_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControllersScreen)
};
