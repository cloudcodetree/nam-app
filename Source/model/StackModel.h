#pragma once
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "json.hpp"

namespace nam {

enum class GearType { Pedal, Amp, Cab, Post };

struct StackChannel {
    std::string toneId, title;
};

struct ChainItem {
    std::string uid;   // stable per item, "i1","i2"... assigned by the model
    GearType type = GearType::Pedal;
    std::string toneId, title, format;   // format "nam"/"ir" as today
    // Original TONE3000 API gear filter this item came from ("pedal","amp",
    // "cab","outboard","space","experimental" -- see Tone3000Api.h and
    // StacksScreen::slotDefs(); "space" is singular, unlike the SPACES slot
    // label); distinct from `type`, which is the chain's coarser routing
    // category.
    std::string gearTag;
    int fs = 0;   // 0 = unassigned, 1..8
    bool bypassed = false;
    std::vector<StackChannel> channels;   // amps only; [0] mirrors toneId/title
    int activeChannel = 0;
    // Unknown v2 JSON keys on this item, preserved verbatim across
    // parse/serialize round-trips so forward-incompatible fields survive.
    nlohmann::json extra = nlohmann::json::object();
};

struct Scene {
    std::string name;
    std::map<std::string, bool> pedalBypass;   // keyed by ChainItem::uid
    int ampChannel = 0;
};

struct Stack {
    std::string name;
    enum class Routing { Single, AB, Stereo };
    Routing routing = Routing::Single;
    std::vector<ChainItem> chain;   // ordered, signal top->bottom
    std::vector<Scene> scenes;
    int activeScene = -1;
    // Unknown v2 JSON keys on this stack, preserved across round-trips.
    nlohmann::json extra = nlohmann::json::object();
};

// JUCE-free ordered-chain stack model: v2 JSON parse/serialize, plus
// auto-migration from the v1 fixed-slot format AppShell::saveStacksState
// shipped with (a top-level array of {"name","slots":[six slot objects]}).
// No public method throws; malformed or unrecognized input degrades to an
// empty result rather than crashing the caller.
class StackModel {
public:
    static std::vector<Stack>
    parse(const std::string& json);   // v2, or v1 auto-migrated; malformed -> {}
    static std::string
    serialize(const std::vector<Stack>& stacks);   // always v2: {"version":2,"stacks":[...]}

    static bool canAdd(const Stack& stack, GearType type);   // false for 2nd Amp/Cab
    static const ChainItem* activeAmp(const Stack& stack);   // first Amp or nullptr
    static const ChainItem* cabOf(const Stack& stack);       // first Cab or nullptr

    static std::string
    activeModelToneId(const Stack& stack);                   // active channel's toneId, "" if none
    static std::string activeIrToneId(const Stack& stack);   // active cab's toneId, "" if none

    struct SceneApply {
        std::string modelToneId, modelTitle;
        std::vector<std::pair<std::string, bool>> bypass;
    };
    // What tapping a scene changes; {} (all empty) for an out-of-range index.
    static SceneApply sceneApplyPlan(const Stack& stack, int sceneIdx);

    static std::string nextUid(const Stack& stack);   // "i{max existing index + 1}"
};

}   // namespace nam
