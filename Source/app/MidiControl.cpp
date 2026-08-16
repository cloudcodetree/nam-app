#include "MidiControl.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace nam {

namespace {

// A cap on how many events can pile up between message-thread drains. A
// stuck expression pedal streams CC at full MIDI rate; without a bound the
// queue would grow without limit while the UI is busy.
constexpr std::size_t kMaxQueued = 256;

}   // namespace

MidiControl::MidiControl() = default;

MidiControl::~MidiControl() {
    // Stop the inputs BEFORE the base AsyncUpdater destructor runs, so no
    // MIDI thread can be mid-callback into a half-destroyed object.
    closeAll();
    cancelPendingUpdate();
}

void MidiControl::refreshDevices() {
    const auto available = juce::MidiInput::getAvailableDevices();
    const auto before = inputs_.size();

    // Drop inputs whose device disappeared (pedal switched off / unpaired).
    for (std::size_t i = inputs_.size(); i-- > 0;) {
        const auto id = inputs_[i]->getIdentifier();
        const bool stillThere = std::any_of(available.begin(), available.end(),
                                            [&id](const auto& d) { return d.identifier == id; });
        if (!stillThere) {
            inputs_[i]->stop();
            inputs_.erase(inputs_.begin() + static_cast<long>(i));
        }
    }

    // Open anything new. Every input is opened rather than just a chosen
    // one: the user may stomp a BLE pedal or a USB one, and guessing which
    // is "the" controller would be wrong as often as right.
    for (const auto& d : available) {
        const bool alreadyOpen = std::any_of(inputs_.begin(), inputs_.end(), [&d](const auto& in) {
            return in->getIdentifier() == d.identifier;
        });
        if (alreadyOpen) continue;

        if (auto in = juce::MidiInput::openDevice(d.identifier, this)) {
            in->start();
            inputs_.push_back(std::move(in));
        }
    }

    if (inputs_.size() != before && onDevicesChanged) onDevicesChanged();
}

void MidiControl::closeAll() {
    for (auto& in : inputs_) in->stop();
    inputs_.clear();
}

std::vector<juce::String> MidiControl::openDeviceNames() const {
    std::vector<juce::String> names;
    names.reserve(inputs_.size());
    for (const auto& in : inputs_) names.push_back(in->getName());
    return names;
}

bool MidiControl::toControlEvent(const juce::MidiMessage& m, ControlEvent& out) {
    out.timeMs = juce::Time::getMillisecondCounter();

    if (m.isController()) {
        out.sig = { ControlKind::Cc, m.getChannel(), m.getControllerNumber() };
        out.value = m.getControllerValue();
        return true;
    }
    if (m.isNoteOnOrOff()) {
        out.sig = { ControlKind::Note, m.getChannel(), m.getNoteNumber() };
        // A note-on with velocity 0 is a note-off by convention.
        out.value = m.isNoteOn() ? juce::jmax(1, static_cast<int>(m.getVelocity())) : 0;
        return true;
    }
    if (m.isProgramChange()) {
        out.sig = { ControlKind::ProgramChange, m.getChannel(), m.getProgramChangeNumber() };
        // A PC has no release half, so it reads as a press every time.
        out.value = 127;
        return true;
    }
    return false;
}

void MidiControl::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) {
    ControlEvent e;
    if (!toControlEvent(m, e)) return;

    {
        const juce::ScopedLock sl(queueLock_);
        // Drop the OLDEST on overflow: the newest stomp is the one the user
        // is waiting on.
        if (queue_.size() >= kMaxQueued) queue_.erase(queue_.begin());
        queue_.push_back(e);
    }
    triggerAsyncUpdate();
}

void MidiControl::handleAsyncUpdate() {
    std::vector<ControlEvent> batch;
    {
        const juce::ScopedLock sl(queueLock_);
        batch.swap(queue_);
    }

    for (const auto& e : batch) {
        // The monitor sees everything, including traffic from switches that
        // are not bound to anything -- that is what makes it useful for
        // discovering what the pedal actually sends.
        if (onEvent) onEvent(e);

        const auto action = map_.handle(e);
        if (action != ControlAction::None && onAction) onAction(action);
    }
}

juce::File MidiControl::bindingsFile() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("NAM Player")
        .getChildFile("controls.json");
}

void MidiControl::load() {
    const auto f = bindingsFile();
    if (!f.existsAsFile()) return;

    const auto text = f.loadFileAsString().toStdString();
    if (text.empty()) return;

    // A corrupt config must not stop the app from starting; an empty map
    // simply means "no bindings yet".
    auto parsed = nlohmann::json::parse(text, nullptr, false);
    if (parsed.is_discarded()) return;

    map_ = ControlMap::fromJson(parsed);
}

void MidiControl::save() const {
    const auto f = bindingsFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText(juce::String(map_.toJson().dump(2)));
}

bool MidiControl::bluetoothPairingAvailable() {
#if JUCE_ANDROID || JUCE_IOS
    return juce::BluetoothMidiDevicePairingDialogue::isAvailable();
#else
    return false;
#endif
}

void MidiControl::showBluetoothPairing(std::function<void(bool)> done) {
#if JUCE_ANDROID || JUCE_IOS
    // JUCE asserts hard if the dialogue is opened without this permission,
    // and the scan silently returns nothing, so the request is not optional.
    juce::RuntimePermissions::request(
        juce::RuntimePermissions::bluetoothMidi, [this, done = std::move(done)](bool granted) {
            if (!granted) {
                if (done) done(false);
                return;
            }
            const bool opened = juce::BluetoothMidiDevicePairingDialogue::open();
            // Pairing adds MIDI ports, but nothing tells us when -- rescan
            // as the dialogue closes so a freshly paired pedal is live
            // without the user hunting for a refresh button.
            if (opened) refreshDevices();
            if (done) done(opened);
        });
#else
    juce::ignoreUnused(this);
    if (done) done(false);
#endif
}

}   // namespace nam
