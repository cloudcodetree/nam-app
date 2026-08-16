#include "ControlMap.h"

#include <algorithm>

namespace nam {

namespace {

struct ActionMeta {
    ControlAction action;
    const char* id;
    const char* label;
};

// Display order for the bindings UI. Ids are PERSISTED -- never rename one.
// Labels are display-only and kept short: the row reserves ~180px for the
// LEARN/policy/clear controls, so longer text elides on a phone.
const ActionMeta kActions[] = {
    { ControlAction::RigNext, "rig.next", "Next rig" },
    { ControlAction::RigPrev, "rig.prev", "Prev rig" },
    { ControlAction::ToneNext, "tone.next", "Next tone" },
    { ControlAction::TonePrev, "tone.prev", "Prev tone" },
    { ControlAction::TunerToggle, "tuner.toggle", "Tuner" },
    { ControlAction::OutputMute, "output.mute", "Mute" },
    { ControlAction::ChainBypass, "chain.bypass", "Bypass" },
    { ControlAction::RigCompare, "rig.compare", "A/B rig" },
};

const char* kindId(ControlKind k) {
    switch (k) {
        case ControlKind::Note: return "note";
        case ControlKind::ProgramChange: return "pc";
        case ControlKind::Cc: break;
    }
    return "cc";
}

ControlKind kindFromId(const std::string& s) {
    if (s == "note") return ControlKind::Note;
    if (s == "pc") return ControlKind::ProgramChange;
    return ControlKind::Cc;
}

}   // namespace

const char* controlActionId(ControlAction a) {
    for (const auto& m : kActions)
        if (m.action == a) return m.id;
    return "";
}

ControlAction controlActionFromId(const std::string& id) {
    for (const auto& m : kActions)
        if (id == m.id) return m.action;
    return ControlAction::None;
}

const char* controlActionLabel(ControlAction a) {
    for (const auto& m : kActions)
        if (m.action == a) return m.label;
    return "";
}

const std::vector<ControlAction>& allControlActions() {
    static const std::vector<ControlAction> all = [] {
        std::vector<ControlAction> v;
        for (const auto& m : kActions) v.push_back(m.action);
        return v;
    }();
    return all;
}

bool ControlSignature::matches(const ControlSignature& in) const {
    if (kind != in.kind || number != in.number) return false;
    // channel 0 == "any", on either side: a wildcard binding accepts any
    // incoming channel, and a wildcard event (a transport that has no
    // channel concept, e.g. BLE HID) matches any binding.
    return channel == 0 || in.channel == 0 || channel == in.channel;
}

bool operator==(const ControlSignature& a, const ControlSignature& b) {
    return a.kind == b.kind && a.channel == b.channel && a.number == b.number;
}

bool operator!=(const ControlSignature& a, const ControlSignature& b) { return !(a == b); }

const char* firePolicyId(FirePolicy p) {
    switch (p) {
        case FirePolicy::Momentary: return "momentary";
        case FirePolicy::Toggle: return "toggle";
        case FirePolicy::Auto: break;
    }
    return "auto";
}

FirePolicy firePolicyFromId(const std::string& s) {
    if (s == "momentary") return FirePolicy::Momentary;
    if (s == "toggle") return FirePolicy::Toggle;
    return FirePolicy::Auto;
}

// Fire decision. A high value is always a press in both pedal modes; the
// only ambiguous event is a LOW one, which is either a momentary pedal's
// release or a toggle pedal's alternate press.
//
// Chris's call (2026-08-16): when Auto cannot tell, guess TOGGLE and fire.
// A dropped stomp is invisible mid-song, whereas a spurious extra action is
// at least legible as a fault.
//
// Auto only has to guess ONCE per switch, though: a low arriving inside
// kMomentaryReleaseWindowMs of its press can only be a foot lifting, so that
// latches st.sawRelease and the switch is treated as momentary from then on.
// That matters because a momentary switch HELD longer than the window would
// otherwise double-fire on release every single time.
bool isControlHigh(ControlKind kind, int value) {
    if (kind == ControlKind::Note) return value > 0;
    return value >= kControlHighThreshold;
}

bool shouldFire(const ControlBinding& binding, const ControlEvent& ev, FireState& st) {
    const bool high = isControlHigh(ev.sig.kind, ev.value);
    const bool wasHigh = isControlHigh(ev.sig.kind, st.lastValue);
    const bool risingEdge = high && !wasHigh;

    // A program change has no value axis at all -- MidiControl synthesizes
    // 127 for every one -- so edge detection would fire once and then leave
    // the binding permanently dead. Each PC IS a discrete press.
    if (ev.sig.kind == ControlKind::ProgramChange) {
        st.lastValue = ev.value;
        return true;
    }

    bool fire = false;

    switch (binding.policy) {
        case FirePolicy::Momentary: fire = risingEdge; break;

        case FirePolicy::Toggle:
            // Every event is one press; the alternating value carries no
            // meaning beyond "the switch moved".
            fire = true;
            break;

        case FirePolicy::Auto:
            if (high) {
                // Only the CROSSING is a press. A continuous controller
                // sweeping past the threshold streams high values, and
                // firing on each would spray actions.
                fire = risingEdge;
            } else if (!wasHigh) {
                // A low that did not follow a high is not a press at all --
                // it is either the first thing this signature ever sent
                // (startup state) or a controller sweeping through its low
                // range. Firing here would let a resting expression pedal
                // trigger actions on its own.
                fire = false;
            } else if (st.sawRelease) {
                // Confirmed momentary: lows are releases, never presses.
                fire = false;
            } else if (ev.timeMs - st.lastEdgeMs <= kMomentaryReleaseWindowMs) {
                // Too fast to be a human pressing again -- this is the
                // release half of a momentary stomp. Latch it.
                st.sawRelease = true;
                fire = false;
            } else {
                fire = true;
            }
            break;
    }

    if (risingEdge) st.lastEdgeMs = ev.timeMs;
    st.lastValue = ev.value;
    return fire;
}

std::string ControlMap::stateKey(const ControlSignature& s) {
    return std::string(kindId(s.kind)) + "|" + std::to_string(s.channel) + "|" +
           std::to_string(s.number);
}

ControlAction ControlMap::handle(const ControlEvent& ev) {
    const bool high = isControlHigh(ev.sig.kind, ev.value);

    // Learn consumes the event rather than also firing it: the switch the
    // user is assigning must not simultaneously trigger whatever it used to
    // be bound to.
    if (learning_ != ControlAction::None) {
        if (!high) return ControlAction::None;
        // Learn channel-agnostically: the pedal's global MIDI channel is
        // user-editable, and pinning the binding to whichever channel
        // happened to arrive would break every binding at once the day it
        // changes.
        ControlSignature learned = ev.sig;
        learned.channel = 0;
        bind(learned, learning_);
        learning_ = ControlAction::None;
        // Seed the fire state from the press that did the learning so the
        // matching release is not read as a fresh stomp.
        auto& st = fireState_[stateKey(ev.sig)];
        st.lastValue = ev.value;
        st.lastEdgeMs = ev.timeMs;
        st.sawRelease = false;
        return ControlAction::None;
    }

    for (const auto& b : bindings_) {
        if (!b.sig.matches(ev.sig)) continue;
        auto& st = fireState_[stateKey(ev.sig)];
        return shouldFire(b, ev, st) ? b.action : ControlAction::None;
    }
    return ControlAction::None;
}

void ControlMap::beginLearn(ControlAction a) { learning_ = a; }
void ControlMap::cancelLearn() { learning_ = ControlAction::None; }

void ControlMap::bind(const ControlSignature& sig, ControlAction action, FirePolicy policy) {
    if (action == ControlAction::None) return;

    // Both directions are 1:1 -- drop any prior binding of this signature
    // AND any prior binding of this action, so a re-learn moves the switch
    // instead of leaving a stale duplicate that double-fires.
    for (std::size_t i = bindings_.size(); i-- > 0;) {
        // Overlap, not exact equality: handle() dispatches by wildcard
        // match, so a channel-0 and a channel-1 binding of the same CC would
        // both persist while only one could ever fire.
        if (bindings_[i].sig.matches(sig) || sig.matches(bindings_[i].sig) ||
            bindings_[i].action == action) {
            bindings_.erase(bindings_.begin() + static_cast<long>(i));
            if (i < extras_.size()) extras_.erase(extras_.begin() + static_cast<long>(i));
        }
    }
    bindings_.push_back({ sig, action, policy });
    extras_.push_back(nlohmann::json::object());
}

void ControlMap::unbind(ControlAction action) {
    for (std::size_t i = bindings_.size(); i-- > 0;) {
        if (bindings_[i].action == action) {
            bindings_.erase(bindings_.begin() + static_cast<long>(i));
            if (i < extras_.size()) extras_.erase(extras_.begin() + static_cast<long>(i));
        }
    }
}

void ControlMap::clear() {
    bindings_.clear();
    extras_.clear();
    fireState_.clear();
    learning_ = ControlAction::None;
}

const ControlBinding* ControlMap::bindingFor(ControlAction a) const {
    for (const auto& b : bindings_)
        if (b.action == a) return &b;
    return nullptr;
}

nlohmann::json ControlMap::toJson() const {
    nlohmann::json out;
    out["version"] = 1;
    auto arr = nlohmann::json::array();
    for (std::size_t i = 0; i < bindings_.size(); ++i) {
        const auto& b = bindings_[i];
        // Start from the preserved unknown keys so a newer build's fields
        // survive; known keys then overwrite their own slots.
        nlohmann::json j =
            (i < extras_.size() && extras_[i].is_object()) ? extras_[i] : nlohmann::json::object();
        j["kind"] = kindId(b.sig.kind);
        j["channel"] = b.sig.channel;
        j["number"] = b.sig.number;
        j["action"] = controlActionId(b.action);
        j["policy"] = firePolicyId(b.policy);
        arr.push_back(std::move(j));
    }
    out["bindings"] = std::move(arr);
    return out;
}

ControlMap ControlMap::fromJson(const nlohmann::json& j) {
    ControlMap m;
    if (!j.is_object()) return m;
    auto it = j.find("bindings");
    if (it == j.end() || !it->is_array()) return m;

    static const char* kKnown[] = { "kind", "channel", "number", "action", "policy" };

    for (const auto& e : *it) {
        if (!e.is_object()) continue;
        ControlBinding b;
        nlohmann::json extra;
        // value() THROWS on a type mismatch (a string where an int belongs)
        // rather than returning the default, and such a file still parses as
        // valid JSON, so the discard check upstream cannot catch it. Same
        // guard StackModel uses, for the same reason: a corrupt config must
        // never stop the app from starting.
        try {
            b.sig.kind = kindFromId(e.value("kind", std::string("cc")));
            b.sig.channel = e.value("channel", 0);
            b.sig.number = e.value("number", 0);
            b.action = controlActionFromId(e.value("action", std::string()));
            b.policy = firePolicyFromId(e.value("policy", std::string("auto")));
            // An action this build does not know parses to None; keep the row
            // so the round-trip is lossless, but it can never fire.
            extra = e;
            for (const char* k : kKnown) extra.erase(k);
        } catch (const nlohmann::json::exception&) {
            // Drop the unreadable row entirely rather than persisting a
            // half-populated binding that would fire on the wrong switch.
            continue;
        }
        m.bindings_.push_back(b);
        m.extras_.push_back(std::move(extra));
    }
    return m;
}

}   // namespace nam
