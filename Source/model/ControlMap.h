#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "json.hpp"

// Transport-agnostic foot-control layer (JUCE-free, unit-tested).
//
// Controllers emit ControlEvents; ControlMap turns them into ControlActions
// via user-owned bindings. MIDI is the first transport (M-Vave Chocolate
// Plus -- see docs/wiki/chocolate-plus.md), but nothing here knows that:
// BLE HID page-turners and the proprietary Spark Control protocol normalize
// into the same ControlEvent, which is the whole point of the layer.
//
// Deliberately NOT here: any hardware default table. The Chocolate's factory
// CC assignments are contested across three sources and user-editable in the
// vendor app anyway, so the app learns what the pedal actually sends rather
// than assuming. See docs/wiki/controllers.md.

namespace nam {

// What a controller can do. The string ids are PERSISTED, so they must stay
// stable across releases even as this enum grows or is reordered.
enum class ControlAction {
    None,
    RigNext,
    RigPrev,
    ToneNext,
    TonePrev,
    TunerToggle,
    OutputMute,
    ChainBypass,
    RigCompare,
};

// Stable persisted id ("rig.next"); "" for None.
const char* controlActionId(ControlAction);
// Round-trips controlActionId; unknown/empty -> None, so a binding written
// by a NEWER build degrades to inert instead of corrupting the file.
ControlAction controlActionFromId(const std::string&);
// Human-facing label for the bindings UI ("Next rig").
const char* controlActionLabel(ControlAction);
// Every real action, in UI display order (excludes None).
const std::vector<ControlAction>& allControlActions();

enum class ControlKind { Cc, Note, ProgramChange };

// Identity of "the thing the user pressed", independent of its value.
struct ControlSignature {
    ControlKind kind = ControlKind::Cc;
    // 1..16. 0 means "any channel" -- the default for learned bindings,
    // because a user who changes the pedal's global channel should not
    // silently lose every binding they made.
    int channel = 0;
    int number = 0;   // CC number, note number, or program number

    bool matches(const ControlSignature& incoming) const;
};

bool operator==(const ControlSignature&, const ControlSignature&);
bool operator!=(const ControlSignature&, const ControlSignature&);

// One normalized press/release, already off the transport's thread.
struct ControlEvent {
    ControlSignature sig;
    int value = 0;              // 0..127 (PC/Note-on synthesize 127)
    std::uint32_t timeMs = 0;   // monotonic; only differences are used
};

// How a binding decides that an event is a "stomp" worth acting on.
enum class FirePolicy {
    Auto,        // infer momentary vs toggle from the traffic (default)
    Momentary,   // pedal sends 127 on press and 0 on release
    Toggle,      // pedal alternates 127 / 0, one event per press
};

const char* firePolicyId(FirePolicy);
FirePolicy firePolicyFromId(const std::string&);

struct ControlBinding {
    ControlSignature sig;
    ControlAction action = ControlAction::None;
    FirePolicy policy = FirePolicy::Auto;
};

// Per-signature runtime state Auto needs to tell the two modes apart.
// Never persisted -- it is a property of the traffic, not the config.
struct FireState {
    int lastValue = 0;
    std::uint32_t lastEdgeMs = 0;   // time of the most recent 0 -> high edge
    bool sawRelease = false;        // a high -> 0 edge has been observed
    bool primed = false;            // any event seen at all yet
};

// A high value is anything at or above this; MIDI switches send 127 but
// nothing guarantees it, and some controllers ramp.
inline constexpr int kControlHighThreshold = 64;

// Auto treats a 0 arriving within this long of the press as the RELEASE half
// of a momentary stomp rather than a fresh toggle press. Anything slower is
// a human pressing again.
inline constexpr std::uint32_t kMomentaryReleaseWindowMs = 400;

// --- the decision -------------------------------------------------------
// Whether `ev` should fire `binding`'s action, given what this signature has
// done before (`st`). Called once per incoming event; `st` is updated for
// the next call. Pure and side-effect-free apart from `st`, so it is fully
// testable -- see tests/test_control_map.cpp.
bool shouldFire(const ControlBinding& binding, const ControlEvent& ev, FireState& st);

class ControlMap {
public:
    // Returns the action to run, or None. Updates internal fire state, so
    // call exactly once per event.
    ControlAction handle(const ControlEvent&);

    // --- learn ----------------------------------------------------------
    // Arms learn: the NEXT event with a high value binds to `action`.
    void beginLearn(ControlAction action);
    void cancelLearn();
    bool isLearning() const { return learning_ != ControlAction::None; }
    ControlAction learningAction() const { return learning_; }

    // --- bindings -------------------------------------------------------
    const std::vector<ControlBinding>& bindings() const { return bindings_; }
    // Binds sig->action, replacing any existing binding for either the same
    // signature or the same action (both are 1:1 -- two switches racing one
    // action, or one switch firing two actions, are both user mistakes we
    // refuse to persist).
    void bind(const ControlSignature&, ControlAction, FirePolicy = FirePolicy::Auto);
    void unbind(ControlAction);
    void clear();
    const ControlBinding* bindingFor(ControlAction) const;

    // --- persistence ----------------------------------------------------
    // Unknown keys are preserved verbatim per binding, same contract as
    // StackModel: a file written by a newer build survives a round-trip
    // through this one.
    nlohmann::json toJson() const;
    static ControlMap fromJson(const nlohmann::json&);

private:
    std::vector<ControlBinding> bindings_;
    std::vector<nlohmann::json> extras_;   // parallel to bindings_
    std::map<std::string, FireState> fireState_;
    ControlAction learning_ = ControlAction::None;

    static std::string stateKey(const ControlSignature&);
};

}   // namespace nam
