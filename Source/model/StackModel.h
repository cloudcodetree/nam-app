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
    // API-supplied artwork URL, captured at the moment a live nam::ToneInfo
    // is available (EDIT picker add/swap) -- ChainItem itself only stores
    // toneId/title/format, so without this a re-fetch later (thumbnail
    // paint) has no imageUrl to hand fetchArtwork and can never succeed even
    // though the id is real. "" for wizard/local-library items (no ToneInfo
    // exists for those) and for files written before this field existed --
    // both fall back to the local-library artwork path or the placeholder.
    std::string imageUrl;
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
    std::string uid;   // stable per stack, "s1","s2"... assigned by the model
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
    // Returned pointer aliases into stack.chain; it is invalidated by any
    // mutation of that chain (push_back/erase/reorder/reassignment) --
    // never retain it across a call that can mutate the stack.
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
    // Unique across the WHOLE file, unlike nextUid (per-stack items) --
    // callers creating a new stack (wizard save/template pick) must pass the
    // full stack list so the mint can't collide with an existing stack's uid.
    static std::string
    nextStackUid(const std::vector<Stack>& stacks);   // "s{max existing index + 1}"
    // Top-level shape check only (valid JSON that's either a v1 array or a
    // v2 object with version:2) -- does NOT guarantee parse() will yield any
    // stacks. Lets a caller distinguish "parse() returned {} because the
    // content is corrupt/unrecognized" from "parse() returned {} because the
    // file is a legitimately empty, well-formed rig list" -- callers that
    // back up unreadable content (see AppShell::loadStacksState) need that
    // distinction so a normal empty-library save can't be mistaken for
    // corruption and overwrite a real recovery backup.
    static bool looksLikeStacksFile(const std::string& json);
};

}   // namespace nam
