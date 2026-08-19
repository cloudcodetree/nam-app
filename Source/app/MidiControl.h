#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>

#include "model/ControlMap.h"

namespace nam {

// MIDI transport for the foot-control layer: opens every available MIDI
// input (USB and BLE alike), normalizes incoming messages into
// ControlEvents, and resolves them to ControlActions through a ControlMap.
//
// THREADING: JUCE delivers MIDI on its own thread -- neither the message
// thread nor the audio thread. Events are queued there under a lock (safe:
// this is NOT the audio thread) and drained on the message thread via
// AsyncUpdater, whose destructor cancels pending updates. Callbacks are
// therefore always invoked on the message thread, so they may touch the UI
// and the engine directly.
//
// The BLE pairing path exists because Android cannot pair a BLE MIDI device
// from system settings -- an app has to call openBluetoothDevice() for the
// MIDI ports to appear at all. See docs/wiki/chocolate-plus.md.
class MidiControl : private juce::MidiInputCallback, private juce::AsyncUpdater {
public:
    MidiControl();
    ~MidiControl() override;

    MidiControl(const MidiControl&) = delete;
    MidiControl& operator=(const MidiControl&) = delete;

    // Fired on the message thread when a bound switch resolves to an action.
    std::function<void(ControlAction)> onAction;
    // Every normalized event, bound or not -- drives the live monitor and
    // makes the learn UI show what the pedal is actually sending.
    std::function<void(ControlEvent)> onEvent;
    // Input list changed (device connected, paired, or lost).
    std::function<void()> onDevicesChanged;
    // The binding map changed under us -- i.e. an incoming press COMPLETED a
    // learn. Fired after the change, so a handler that re-reads the map sees
    // the new state; wiring UI refresh to onEvent instead reads it too early
    // and leaves the row stuck showing "press a switch...".
    std::function<void()> onMapChanged;

    // Opens every MIDI input not already open, and drops ones that vanished.
    // Safe to call repeatedly; that is how a newly paired pedal is picked up.
    void refreshDevices();
    void closeAll();

    // Names of the currently open inputs, for the settings UI.
    std::vector<juce::String> openDeviceNames() const;
    bool hasOpenDevice() const { return !inputs_.empty(); }

    // Runs an event from a NON-MIDI transport through the same ControlMap
    // (BLE HID keyboards -- see ControlKind::Key). Returns true if it was
    // consumed: either it fired an action or learn captured it. Callers use
    // that to decide whether to swallow the input, so an unbound key still
    // reaches whatever else wanted it. Message thread only.
    bool dispatchEvent(const ControlEvent&);

    // --- bindings (message thread only) ---------------------------------
    ControlMap& map() { return map_; }
    const ControlMap& map() const { return map_; }

    // Persists to <appdata>/NAM Player/controls.json. Missing/corrupt file
    // leaves the map empty rather than throwing -- an unreadable config must
    // never stop the app from starting.
    void load();
    void save() const;
    static juce::File bindingsFile();

    // --- Bluetooth LE pairing (Android/iOS) -----------------------------
    // True when the platform can pair BLE MIDI at all.
    static bool bluetoothPairingAvailable();
    // Requests the bluetoothMidi runtime permission, then opens JUCE's
    // pairing dialogue. `done` runs on the message thread with whether the
    // dialogue actually opened. Never asserts on a denied permission.
    void showBluetoothPairing(std::function<void(bool)> done);

private:
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override;
    void handleAsyncUpdate() override;

    // Returns false for messages that carry no switch semantics (clock,
    // aftertouch, sysex...), which are dropped rather than queued.
    static bool toControlEvent(const juce::MidiMessage&, ControlEvent&);

    // Liveness token for callbacks that can outlive this object (the BLE
    // pairing dialogue). Reset in the destructor; observers hold weak refs.
    std::shared_ptr<int> alive_ = std::make_shared<int>(0);

    std::vector<std::unique_ptr<juce::MidiInput>> inputs_;
    ControlMap map_;

    juce::CriticalSection queueLock_;
    std::vector<ControlEvent> queue_;
};

}   // namespace nam
