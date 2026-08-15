#include "model/StackModel.h"

#include <array>

using json = nlohmann::json;

namespace nam {

namespace {

// --- enum <-> string -------------------------------------------------------

std::string typeToString(GearType t) {
    switch (t) {
        case GearType::Amp: return "amp";
        case GearType::Cab: return "cab";
        case GearType::Post: return "post";
        case GearType::Pedal:
        default: return "pedal";
    }
}

GearType typeFromString(const std::string& s) {
    if (s == "amp") return GearType::Amp;
    if (s == "cab") return GearType::Cab;
    if (s == "post") return GearType::Post;
    return GearType::Pedal;
}

// --- v2 item/stack <-> json -------------------------------------------

constexpr std::array<const char*, 10> kItemKeys{ "uid",      "type",         "toneId",   "title",
                                                 "format",   "gearTag",      "imageUrl", "bypassed",
                                                 "channels", "activeChannel" };

ChainItem itemFromJson(const json& cj) {
    ChainItem it;
    it.uid = cj.value("uid", std::string());
    it.type = typeFromString(cj.value("type", std::string("pedal")));
    it.toneId = cj.value("toneId", std::string());
    it.title = cj.value("title", std::string());
    it.format = cj.value("format", std::string());
    it.gearTag = cj.value("gearTag", std::string());
    it.imageUrl = cj.value("imageUrl", std::string());
    it.bypassed = cj.value("bypassed", false);
    if (cj.contains("channels") && cj.at("channels").is_array())
        for (const auto& ch : cj.at("channels"))
            if (ch.is_object())
                it.channels.push_back(
                    { ch.value("toneId", std::string()), ch.value("title", std::string()) });
    it.activeChannel = cj.value("activeChannel", 0);

    json extra = cj.is_object() ? cj : json::object();
    for (const char* k : kItemKeys) extra.erase(k);
    it.extra = extra;
    return it;
}

json itemToJson(const ChainItem& it) {
    json j = it.extra.is_object() ? it.extra : json::object();
    j["uid"] = it.uid;
    j["type"] = typeToString(it.type);
    j["toneId"] = it.toneId;
    j["title"] = it.title;
    j["format"] = it.format;
    j["gearTag"] = it.gearTag;
    j["imageUrl"] = it.imageUrl;
    j["bypassed"] = it.bypassed;
    json channels = json::array();
    for (const auto& ch : it.channels)
        channels.push_back({ { "toneId", ch.toneId }, { "title", ch.title } });
    j["channels"] = channels;
    j["activeChannel"] = it.activeChannel;
    return j;
}

constexpr std::array<const char*, 3> kStackKeys{ "uid", "name", "chain" };

Stack stackFromJson(const json& sj) {
    Stack st;
    st.uid = sj.value("uid", std::string());
    st.name = sj.value("name", std::string());
    // Per-item isolation: one malformed chain item (wrong field type) drops
    // only that item, not the whole stack.
    if (sj.contains("chain") && sj.at("chain").is_array())
        for (const auto& cj : sj.at("chain")) {
            if (!cj.is_object()) continue;
            try {
                st.chain.push_back(itemFromJson(cj));
            } catch (const std::exception&) {
                // malformed item; skip it, keep the rest of the stack
            }
        }

    json extra = sj.is_object() ? sj : json::object();
    for (const char* k : kStackKeys) extra.erase(k);
    st.extra = extra;
    return st;
}

json stackToJson(const Stack& st) {
    json j = st.extra.is_object() ? st.extra : json::object();
    j["uid"] = st.uid;
    j["name"] = st.name;
    json chain = json::array();
    for (const auto& it : st.chain) chain.push_back(itemToJson(it));
    j["chain"] = chain;
    return j;
}

std::vector<Stack> parseV2(const json& root) {
    std::vector<Stack> out;
    if (!root.contains("stacks") || !root.at("stacks").is_array()) return out;
    // Per-stack isolation: one malformed stack (e.g. a wrong-typed
    // top-level field) drops only that stack, not the user's whole library.
    for (const auto& sj : root.at("stacks")) {
        if (!sj.is_object()) continue;
        try {
            out.push_back(stackFromJson(sj));
        } catch (const std::exception&) {
            // malformed stack; skip it, keep the rest of the file
        }
    }
    return out;
}

// --- v1 (shipped fixed-slot) migration --------------------------------------

struct V1SlotDef {
    GearType type;
    const char* gearTag;
};

// AMP, CABINET, PEDAL, OUTBOARD, SPACES, EXPERIMENTAL -- the exact order
// StacksScreen::slotDefs()/AppShell::saveStacksState wrote. gearTag values
// are the live TONE3000 API gear filter; Tone3000Api.h is the SOLE
// authority for valid values (amp-cab|amp|cab|pedal|outboard|space|
// experimental). Do NOT copy StacksScreen::slotDefs()'s gearApi column
// verbatim -- its CABINET row is "ir" (a format filter for that picker,
// not an API gear), while the correct gearTag here is "cab".
constexpr std::array<V1SlotDef, 6> kV1Slots{ { { GearType::Amp, "amp" },
                                               { GearType::Cab, "cab" },
                                               { GearType::Pedal, "pedal" },
                                               { GearType::Post, "outboard" },
                                               { GearType::Post, "space" },
                                               { GearType::Post, "experimental" } } };

Stack migrateV1Stack(const json& sj) {
    Stack st;
    st.name = sj.value("name", std::string());
    if (!sj.contains("slots") || !sj.at("slots").is_array()) return st;

    const auto& slots = sj.at("slots");
    int nextIdx = 1;
    // Per-slot isolation: one malformed slot (e.g. a wrong-typed "id")
    // drops only that slot, not the whole rig.
    for (size_t k = 0; k < slots.size() && k < kV1Slots.size(); ++k) {
        const auto& so = slots[k];
        if (!so.is_object()) continue;
        try {
            std::string id = so.value("id", std::string());
            if (id.empty()) continue;   // empty slot -> skipped, not a chain item

            ChainItem it;
            it.type = kV1Slots[k].type;
            it.gearTag = kV1Slots[k].gearTag;
            it.toneId = id;
            it.title = so.value("title", std::string());
            it.format = so.value("format", std::string());
            if (it.type == GearType::Amp) {
                it.channels.push_back({ it.toneId, it.title });
                it.activeChannel = 0;
            }
            it.uid = "i" + std::to_string(nextIdx);
            st.chain.push_back(std::move(it));
            ++nextIdx;
        } catch (const std::exception&) {
            // malformed slot; skip it, keep the rest of the rig
        }
    }
    return st;
}

std::vector<Stack> migrateV1(const json& arr) {
    std::vector<Stack> out;
    // Per-stack isolation, same reasoning as parseV2.
    for (const auto& sj : arr) {
        if (!sj.is_object()) continue;
        try {
            out.push_back(migrateV1Stack(sj));
        } catch (const std::exception&) {
            // malformed rig; skip it, keep the rest of the file
        }
    }
    return out;
}

// --- stack uid minting -------------------------------------------------

// Ceiling for a recognized "sN" suffix -- comfortably beyond any real stack
// count, but far enough below INT_MAX that maxN + 1 (nextStackUid) can never
// signed-overflow (UB). A hand-edited uid at or past this (e.g. exactly
// "s2147483647", which stoi parses without throwing) is treated the same as
// a non-numeric suffix: ignored rather than trusted.
constexpr int kMaxSaneStackUidIndex = 1'000'000'000;

// Shared by nextStackUid and assignMissingStackUids: highest "sN" index
// already present, or 0 if none. Non-numeric/malformed/adversarial suffixes
// are ignored rather than thrown on, same tolerance as ChainItem's nextUid.
int maxStackUidIndex(const std::vector<Stack>& stacks) {
    int maxN = 0;
    for (const auto& st : stacks) {
        if (st.uid.size() > 1 && st.uid[0] == 's') {
            try {
                long n = std::stol(st.uid.substr(1));
                if (n > maxN && n < kMaxSaneStackUidIndex) maxN = (int)n;
            } catch (const std::exception&) {
                // non-numeric or out-of-range suffix; ignore
            }
        }
    }
    return maxN;
}

// A stack loaded from a file that predates Stack::uid (any v1 file, or a v2
// file written before this field existed) has an empty uid -- mint one for
// every such stack, monotonic against the highest index already present so
// an auto-minted uid can never collide with an explicit one elsewhere in
// the same file (see the "Has Uid" mixed-fixture test).
void assignMissingStackUids(std::vector<Stack>& stacks) {
    int nextN = maxStackUidIndex(stacks);
    for (auto& st : stacks)
        if (st.uid.empty()) st.uid = "s" + std::to_string(++nextN);
    // A hand-edited file can carry two stacks with the SAME explicit uid --
    // the (index,uid) revalidation in AppShellStackApply.cpp assumes
    // uniqueness, so every occurrence past the first is re-minted fresh
    // rather than left to collide.
    std::vector<std::string> seen;
    seen.reserve(stacks.size());
    for (auto& st : stacks) {
        bool dup = false;
        for (const auto& s : seen)
            if (s == st.uid) {
                dup = true;
                break;
            }
        if (dup) st.uid = "s" + std::to_string(++nextN);
        seen.push_back(st.uid);
    }
}

}   // namespace

std::vector<Stack> StackModel::parse(const std::string& jsonText) {
    try {
        json root = json::parse(jsonText);
        std::vector<Stack> out;
        if (root.is_array()) out = migrateV1(root);
        else if (root.is_object() && root.value("version", 0) == 2) out = parseV2(root);
        else return {};
        assignMissingStackUids(out);
        return out;
    } catch (const std::exception&) { return {}; }
}

bool StackModel::looksLikeStacksFile(const std::string& jsonText) {
    try {
        json root = json::parse(jsonText);
        return root.is_array() || (root.is_object() && root.value("version", 0) == 2);
    } catch (const std::exception&) { return false; }
}

std::string StackModel::serialize(const std::vector<Stack>& stacks) {
    try {
        json arr = json::array();
        for (const auto& st : stacks) arr.push_back(stackToJson(st));
        json root{ { "version", 2 }, { "stacks", arr } };
        // error_handler_t::replace swaps invalid UTF-8 byte sequences for
        // U+FFFD instead of throwing type_error.316 -- upholds the "no
        // public method throws" contract even if a string field somehow
        // carries non-UTF-8 bytes.
        return root.dump(2, ' ', false, json::error_handler_t::replace);
    } catch (const std::exception&) {
        // Should be unreachable given the replace handler above, but keep
        // the contract airtight against any other construction failure.
        return R"({"version":2,"stacks":[]})";
    }
}

bool StackModel::canAdd(const Stack& stack, GearType type) {
    if (type != GearType::Amp && type != GearType::Cab) return true;
    for (const auto& it : stack.chain)
        if (it.type == type) return false;
    return true;
}

const ChainItem* StackModel::activeAmp(const Stack& stack) {
    for (const auto& it : stack.chain)
        if (it.type == GearType::Amp) return &it;
    return nullptr;
}

const ChainItem* StackModel::cabOf(const Stack& stack) {
    for (const auto& it : stack.chain)
        if (it.type == GearType::Cab) return &it;
    return nullptr;
}

std::string StackModel::activeModelToneId(const Stack& stack) {
    const ChainItem* amp = activeAmp(stack);
    if (amp == nullptr) return {};
    if (!amp->channels.empty() && amp->activeChannel >= 0 &&
        amp->activeChannel < (int)amp->channels.size())
        return amp->channels[(size_t)amp->activeChannel].toneId;
    return amp->toneId;
}

std::string StackModel::activeIrToneId(const Stack& stack) {
    const ChainItem* cab = cabOf(stack);
    return cab != nullptr ? cab->toneId : std::string();
}

std::string StackModel::nextUid(const Stack& stack) {
    int maxN = 0;
    for (const auto& it : stack.chain) {
        if (it.uid.size() > 1 && it.uid[0] == 'i') {
            try {
                int n = std::stoi(it.uid.substr(1));
                if (n > maxN) maxN = n;
            } catch (const std::exception&) {
                // non-numeric suffix; ignore
            }
        }
    }
    return "i" + std::to_string(maxN + 1);
}

std::string StackModel::nextStackUid(const std::vector<Stack>& stacks) {
    return "s" + std::to_string(maxStackUidIndex(stacks) + 1);
}

}   // namespace nam
