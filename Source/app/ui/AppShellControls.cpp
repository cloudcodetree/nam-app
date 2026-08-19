#include "app/ui/AppShell.h"

// Foot control: owns the wiring between MidiControl (the transport) and
// ControllersScreen (the setup UI), plus the dispatch that makes an action
// actually do something. Lives here rather than in AppShell.cpp, which is
// already past the 800-line ceiling.
//
// Everything in this file runs on the message thread: MidiControl drains its
// MIDI-thread queue through an AsyncUpdater before invoking onAction/onEvent,
// so these handlers may touch the UI and the engine directly.

using nam::ControlAction;
using nam::ControlBinding;
using nam::FirePolicy;

void AppShell::wireControllers () {
    // Constructed AND parented here rather than in the AppShell constructor:
    // AppShell.cpp is past the 800-line ceiling, so this feature keeps its
    // footprint there to navigation only (the Screen enumerator, show()'s
    // target, and the ⋯ menu hit).
    controllers_ = std::make_unique<ControllersScreen> ();
    midi_ = std::make_unique<nam::MidiControl> ();
    addChildComponent (*controllers_);

    controllers_->onBack = [this] { show (Screen::Play); };

    controllers_->setPairingAvailable (nam::MidiControl::bluetoothPairingAvailable ());

    controllers_->onPair = [this] {
        midi_->showBluetoothPairing ([this] (bool) {
            // Whether or not the dialogue opened, re-derive the device list:
            // a denied permission must still leave the screen truthful.
            pushControllerState ();
        });
    };
    controllers_->onRescan = [this] {
        midi_->refreshDevices ();
        pushControllerState ();
    };

    controllers_->onLearn = [this] (ControlAction a) {
        // Tapping LEARN on the row that is already learning cancels it,
        // rather than leaving the user stuck in an armed state with no exit.
        if (midi_->map ().isLearning () && midi_->map ().learningAction () == a)
            midi_->map ().cancelLearn ();
        else midi_->map ().beginLearn (a);
        pushControllerState ();
    };
    controllers_->onClear = [this] (ControlAction a) {
        midi_->map ().unbind (a);
        midi_->save ();
        pushControllerState ();
    };
    controllers_->onCyclePolicy = [this] (ControlAction a) {
        const auto* b = midi_->map ().bindingFor (a);
        if (b == nullptr) return;   // nothing bound: nothing to re-policy
        const auto next = b->policy == FirePolicy::Auto        ? FirePolicy::Momentary
                          : b->policy == FirePolicy::Momentary ? FirePolicy::Toggle
                                                               : FirePolicy::Auto;
        // bind() replaces in place, keeping the signature.
        midi_->map ().bind (b->sig, a, next);
        midi_->save ();
        pushControllerState ();
    };

    midi_->onDevicesChanged = [this] { pushControllerState (); };
    midi_->onEvent = [this] (nam::ControlEvent e) {
        // Only the visible setup screen cares about raw traffic; feeding it
        // while elsewhere would repaint a hidden component every stomp.
        if (current_ == controllers_.get ()) controllers_->pushEvent (e);
    };
    midi_->onMapChanged = [this] {
        // A press completed a learn: persist it (otherwise the binding is
        // lost on restart) and re-read the rows.
        midi_->save ();
        pushControllerState ();
    };
    midi_->onAction = [this] (ControlAction a) { runControlAction (a); };

    midi_->load ();
    midi_->refreshDevices ();

    // A BLE HID pedal delivers key events, and they only reach this component
    // if it can hold focus. Keys the control layer does not claim are passed
    // straight back, so this never eats normal input.
    setWantsKeyboardFocus (true);
}

bool AppShell::keyPressed (const juce::KeyPress& k) {
    if (midi_ == nullptr) return false;
    nam::ControlEvent e;
    e.sig = { nam::ControlKind::Key, 0, k.getKeyCode () };
    e.value = 127;   // a key press has no release half; it IS the press
    e.timeMs = (std::uint32_t)juce::Time::getMillisecondCounter ();
    return midi_->dispatchEvent (e);
}

void AppShell::pushControllerState () {
    if (controllers_ == nullptr || midi_ == nullptr) return;
    controllers_->setDevices (midi_->openDeviceNames ());
    controllers_->setBindings (midi_->map ().bindings ());
    controllers_->setLearning (midi_->map ().isLearning () ? midi_->map ().learningAction ()
                                                           : ControlAction::None);
}

void AppShell::runControlAction (ControlAction a) {
    switch (a) {
        case ControlAction::RigNext: stepStack (1); break;
        case ControlAction::RigPrev: stepStack (-1); break;

        // Play's ‹ › through the library. Deliberately works from any
        // screen: a foot controller is for when your hands are busy, so
        // forcing the user to navigate first would defeat it.
        case ControlAction::ToneNext: stepCollection (1); break;
        case ControlAction::TonePrev: stepCollection (-1); break;

        case ControlAction::TunerToggle:
            // The tuner is a Play overlay, so make Play visible first --
            // otherwise it opens behind whatever screen is up.
            if (current_ != play_.get ()) show (Screen::Play);
            toggleTuner ();
            break;

        case ControlAction::OutputMute: setControlMute (!controlMuted_); break;

        case ControlAction::ChainBypass: engine_.setBypassed (!engine_.isBypassed ()); break;

        case ControlAction::RigCompare: swapToPreviousStack (); break;

        case ControlAction::None: break;
    }
}

void AppShell::setControlMute (bool mute) {
    if (mute == controlMuted_) return;
    if (mute) {
        // Remember what the user had, so unmuting restores their level
        // rather than snapping to a hardcoded default.
        preMuteOutputDb_ = engine_.outputDb ();
        engine_.setOutputDb (-100.0f);
    } else {
        engine_.setOutputDb (preMuteOutputDb_);
    }
    controlMuted_ = mute;
}

void AppShell::stepStack (int delta) {
    if (stackList_.empty ()) return;

    // Where are we? currentIndex() is -1 until a rig has been opened, in
    // which case a first stomp should land on the first rig rather than
    // jumping to the end.
    const int openIdx = stacksDetail_ != nullptr ? stacksDetail_->currentIndex () : -1;
    int idx = openIdx;
    if (idx < 0) idx = delta > 0 ? -1 : 0;
    idx = (int)(((idx + delta) % (int)stackList_.size () + (int)stackList_.size ()) %
                (int)stackList_.size ());

    if (idx == openIdx) return;
    previousStackIdx_ = openIdx;
    openStackDetail (idx);
}

void AppShell::swapToPreviousStack () {
    // A/B: flip between the current rig and the last one, so a single switch
    // toggles back and forth rather than walking forward.
    if (previousStackIdx_ < 0 || previousStackIdx_ >= (int)stackList_.size ()) return;
    const int target = previousStackIdx_;
    previousStackIdx_ = stacksDetail_ != nullptr ? stacksDetail_->currentIndex () : -1;
    openStackDetail (target);
}
