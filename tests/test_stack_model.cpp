#include <catch2/catch_all.hpp>
#include "json.hpp"
#include "model/StackModel.h"

using namespace nam;
using json = nlohmann::json;

TEST_CASE("StackModel v2 round-trip preserves every field incl. unknown keys") {
    // "routing"/"scenes"/"activeScene" (stack-level) and "fs" (item-level)
    // are no longer model fields -- Stacks is a plain ordered chain, no
    // guided/routing/scene concept exists. They're deliberately included
    // here as UNKNOWN keys: shrinking kStackKeys/kItemKeys means parse()'s
    // per-key `extra.erase(k)` no longer touches them, so a file written by
    // a prior build (or a hand-edited one) round-trips them untouched
    // instead of silently losing them.
    static const char* v2Fixture = R"JSON(
    {
      "version": 2,
      "stacks": [
        {
          "name": "My Rig",
          "uid": "s7",
          "routing": "ab",
          "notes": "custom-stack-field",
          "chain": [
            {
              "uid": "i1",
              "type": "amp",
              "toneId": "amp-1",
              "title": "Amp One",
              "format": "nam",
              "gearTag": "amp",
              "imageUrl": "https://example.com/amp-1.jpg",
              "fs": 3,
              "bypassed": false,
              "channels": [
                {"toneId": "amp-1", "title": "Amp One"},
                {"toneId": "amp-1b", "title": "Amp One Ch B"}
              ],
              "activeChannel": 1,
              "vendorExtra": 42
            }
          ],
          "scenes": [
            {"name": "Clean", "pedalBypass": {"i2": true}, "ampChannel": 0}
          ],
          "activeScene": 0
        }
      ]
    }
    )JSON";

    auto stacks = StackModel::parse(v2Fixture);
    REQUIRE(stacks.size() == 1);
    const auto& st = stacks[0];
    REQUIRE(st.name == "My Rig");
    REQUIRE(st.uid == "s7");   // explicit uid preserved verbatim, not re-minted
    REQUIRE(st.chain.size() == 1);
    // Retired stack-level keys survive verbatim in extra, untouched.
    REQUIRE(st.extra.at("routing") == "ab");
    REQUIRE(st.extra.at("activeScene") == 0);
    REQUIRE(st.extra.at("scenes")[0]["name"] == "Clean");
    REQUIRE(st.extra.at("notes") == "custom-stack-field");

    const auto& item = st.chain[0];
    REQUIRE(item.uid == "i1");
    REQUIRE(item.type == GearType::Amp);
    REQUIRE(item.toneId == "amp-1");
    REQUIRE(item.title == "Amp One");
    REQUIRE(item.format == "nam");
    REQUIRE(item.gearTag == "amp");
    REQUIRE(item.imageUrl == "https://example.com/amp-1.jpg");
    REQUIRE_FALSE(item.bypassed);
    REQUIRE(item.channels.size() == 2);
    REQUIRE(item.channels[1].toneId == "amp-1b");
    REQUIRE(item.activeChannel == 1);
    // Retired item-level key ("fs") and a genuinely-unknown one both survive.
    REQUIRE(item.extra.at("fs") == 3);
    REQUIRE(item.extra.at("vendorExtra") == 42);

    auto out = StackModel::serialize(stacks);
    json reparsed = json::parse(out);
    REQUIRE(reparsed["version"] == 2);
    REQUIRE(reparsed["stacks"][0]["uid"] == "s7");
    // Unknown keys on both stack and item objects survive the round trip.
    REQUIRE(reparsed["stacks"][0]["notes"] == "custom-stack-field");
    REQUIRE(reparsed["stacks"][0]["routing"] == "ab");
    REQUIRE(reparsed["stacks"][0]["scenes"][0]["name"] == "Clean");
    REQUIRE(reparsed["stacks"][0]["activeScene"] == 0);
    REQUIRE(reparsed["stacks"][0]["chain"][0]["vendorExtra"] == 42);
    REQUIRE(reparsed["stacks"][0]["chain"][0]["fs"] == 3);
    REQUIRE(reparsed["stacks"][0]["chain"][0]["imageUrl"] == "https://example.com/amp-1.jpg");

    auto stacks2 = StackModel::parse(out);
    REQUIRE(stacks2.size() == 1);
    REQUIRE(stacks2[0].uid == "s7");   // still preserved after a second round trip
    REQUIRE(stacks2[0].chain[0].activeChannel == 1);
    REQUIRE(stacks2[0].chain[0].channels.size() == 2);
    REQUIRE(stacks2[0].extra.at("routing") == "ab");
    REQUIRE(stacks2[0].chain[0].extra.at("fs") == 3);
    REQUIRE(stacks2[0].chain[0].imageUrl == "https://example.com/amp-1.jpg");
}

TEST_CASE("StackModel parse defaults imageUrl to empty when the key is absent") {
    // Files written before this field existed (and any wizard/local-library
    // item, which never has a fetchable URL to begin with) must parse to an
    // empty imageUrl rather than throwing or leaving it uninitialized --
    // artworkForChainItem's fetch-on-miss route treats "" as "no URL, fall
    // back to the local-library path or the placeholder."
    static const char* noImageUrlFixture = R"JSON(
    {
      "version": 2,
      "stacks": [
        {
          "name": "Old Rig",
          "uid": "s1",
          "chain": [
            {"uid": "i1", "type": "pedal", "toneId": "pedal-1", "title": "P",
             "format": "nam", "gearTag": "pedal", "fs": 0, "bypassed": false}
          ]
        }
      ]
    }
    )JSON";

    auto stacks = StackModel::parse(noImageUrlFixture);
    REQUIRE(stacks.size() == 1);
    REQUIRE(stacks[0].chain.size() == 1);
    REQUIRE(stacks[0].chain[0].imageUrl.empty());
}

TEST_CASE("StackModel v1 migration maps fixed slots to ordered chain") {
    // Exact shipped shape from AppShell::saveStacksState: top-level ARRAY of
    // {"name","slots":[six {"id","title","format"}]}, slot order
    // AMP, CABINET, PEDAL, OUTBOARD, SPACES, EXPERIMENTAL.
    static const char* v1Fixture = R"JSON(
    [
      {
        "name": "Old Rig",
        "slots": [
          {"id": "amp-x", "title": "Amp X", "format": "nam"},
          {"id": "cab-y", "title": "Cab Y", "format": "ir"},
          {"id": "pedal-z", "title": "Pedal Z", "format": "nam"},
          {"id": "", "title": "", "format": ""},
          {"id": "space-w", "title": "Space W", "format": "ir"},
          {"id": "exp-v", "title": "Exp V", "format": "nam"}
        ]
      }
    ]
    )JSON";

    auto stacks = StackModel::parse(v1Fixture);
    REQUIRE(stacks.size() == 1);
    const auto& st = stacks[0];
    REQUIRE(st.name == "Old Rig");
    REQUIRE(st.uid == "s1");   // v1 files carry no stack uid; parse mints one
    // Empty OUTBOARD slot skipped -> 5 items, not 6.
    REQUIRE(st.chain.size() == 5);

    REQUIRE(st.chain[0].type == GearType::Amp);
    REQUIRE(st.chain[0].gearTag == "amp");
    REQUIRE(st.chain[0].toneId == "amp-x");
    REQUIRE(st.chain[0].title == "Amp X");
    REQUIRE(st.chain[0].uid == "i1");
    REQUIRE(st.chain[0].channels.size() == 1);
    REQUIRE(st.chain[0].channels[0].toneId == "amp-x");
    REQUIRE(st.chain[0].activeChannel == 0);
    // v1 has no imageUrl concept -- migration must leave it empty, not
    // fabricate one, so artworkForChainItem's fetch-on-miss route falls
    // back to the local-library path instead of trying a bogus URL.
    REQUIRE(st.chain[0].imageUrl.empty());

    REQUIRE(st.chain[1].type == GearType::Cab);
    REQUIRE(st.chain[1].gearTag == "cab");
    REQUIRE(st.chain[1].toneId == "cab-y");
    REQUIRE(st.chain[1].uid == "i2");

    REQUIRE(st.chain[2].type == GearType::Pedal);
    REQUIRE(st.chain[2].gearTag == "pedal");
    REQUIRE(st.chain[2].toneId == "pedal-z");
    REQUIRE(st.chain[2].uid == "i3");

    // OUTBOARD (empty) skipped; SPACES is next.
    REQUIRE(st.chain[3].type == GearType::Post);
    // Live TONE3000 API gear is "space" (singular) per StacksScreen::slotDefs()
    // and Tone3000Api.h, not the "SPACES" slot label.
    REQUIRE(st.chain[3].gearTag == "space");
    REQUIRE(st.chain[3].toneId == "space-w");
    REQUIRE(st.chain[3].uid == "i4");

    REQUIRE(st.chain[4].type == GearType::Post);
    REQUIRE(st.chain[4].gearTag == "experimental");
    REQUIRE(st.chain[4].toneId == "exp-v");
    REQUIRE(st.chain[4].uid == "i5");
}

TEST_CASE("StackModel v1 migration with an all-empty stack yields an empty chain") {
    static const char* v1Fixture = R"JSON(
    [
      {"name": "Blank", "slots": [
        {"id": "", "title": "", "format": ""},
        {"id": "", "title": "", "format": ""},
        {"id": "", "title": "", "format": ""},
        {"id": "", "title": "", "format": ""},
        {"id": "", "title": "", "format": ""},
        {"id": "", "title": "", "format": ""}
      ]}
    ]
    )JSON";
    auto stacks = StackModel::parse(v1Fixture);
    REQUIRE(stacks.size() == 1);
    REQUIRE(stacks[0].name == "Blank");
    REQUIRE(stacks[0].chain.empty());
}

TEST_CASE("StackModel canAdd allows only one Amp and one Cab") {
    Stack s;
    REQUIRE(StackModel::canAdd(s, GearType::Amp));
    REQUIRE(StackModel::canAdd(s, GearType::Cab));
    REQUIRE(StackModel::canAdd(s, GearType::Pedal));
    REQUIRE(StackModel::canAdd(s, GearType::Post));

    ChainItem amp;
    amp.uid = "i1";
    amp.type = GearType::Amp;
    s.chain.push_back(amp);
    REQUIRE_FALSE(StackModel::canAdd(s, GearType::Amp));
    REQUIRE(StackModel::canAdd(s, GearType::Cab));
    REQUIRE(StackModel::canAdd(s, GearType::Pedal));   // multiple pedals allowed

    ChainItem cab;
    cab.uid = "i2";
    cab.type = GearType::Cab;
    s.chain.push_back(cab);
    REQUIRE_FALSE(StackModel::canAdd(s, GearType::Cab));

    ChainItem pedal1;
    pedal1.uid = "i3";
    pedal1.type = GearType::Pedal;
    s.chain.push_back(pedal1);
    REQUIRE(StackModel::canAdd(s, GearType::Pedal));   // still fine, no cap on pedals

    REQUIRE(StackModel::activeAmp(s) != nullptr);
    REQUIRE(StackModel::activeAmp(s)->uid == "i1");
    REQUIRE(StackModel::cabOf(s)->uid == "i2");
}

TEST_CASE("StackModel activeAmp/cabOf return nullptr when absent") {
    Stack s;
    REQUIRE(StackModel::activeAmp(s) == nullptr);
    REQUIRE(StackModel::cabOf(s) == nullptr);
    REQUIRE(StackModel::activeModelToneId(s).empty());
    REQUIRE(StackModel::activeIrToneId(s).empty());
}

TEST_CASE("StackModel activeModelToneId/activeIrToneId read the active channel") {
    Stack s;
    ChainItem amp;
    amp.uid = "i1";
    amp.type = GearType::Amp;
    amp.toneId = "clean-tone";
    amp.title = "Clean";
    amp.channels = { { "clean-tone", "Clean" }, { "lead-tone", "Lead" } };
    amp.activeChannel = 1;
    s.chain.push_back(amp);

    ChainItem cab;
    cab.uid = "i2";
    cab.type = GearType::Cab;
    cab.toneId = "cab-ir";
    s.chain.push_back(cab);

    REQUIRE(StackModel::activeModelToneId(s) == "lead-tone");
    REQUIRE(StackModel::activeIrToneId(s) == "cab-ir");
}

TEST_CASE("StackModel parse: malformed JSON never throws, returns empty vector") {
    REQUIRE(StackModel::parse("not valid json {{{").empty());
    REQUIRE(StackModel::parse("").empty());
    REQUIRE(StackModel::parse("null").empty());
    REQUIRE(StackModel::parse("42").empty());
    REQUIRE(StackModel::parse("\"just a string\"").empty());
    // Well-formed JSON but neither v1 (array) nor v2 (object w/ version:2).
    REQUIRE(StackModel::parse(R"({"version":1,"stacks":[]})").empty());
    REQUIRE(StackModel::parse(R"({"foo":"bar"})").empty());
}

TEST_CASE("StackModel nextUid is monotonic based on the highest existing index") {
    Stack s;
    REQUIRE(StackModel::nextUid(s) == "i1");

    ChainItem a;
    a.uid = "i1";
    s.chain.push_back(a);
    REQUIRE(StackModel::nextUid(s) == "i2");

    ChainItem b;
    b.uid = "i5";
    s.chain.push_back(b);
    REQUIRE(StackModel::nextUid(s) == "i6");

    ChainItem c;
    c.uid = "i3";
    s.chain.push_back(c);
    REQUIRE(StackModel::nextUid(s) == "i6");   // still based on max (i5), not count
}

TEST_CASE("StackModel parse v2: a malformed chain item is skipped, its stack and others survive") {
    // stack[0]'s second item has "bypassed" as a string (should be bool) --
    // only that ONE item should be dropped, not the whole stack or the file.
    static const char* fixture = R"JSON(
    {
      "version": 2,
      "stacks": [
        {
          "name": "Mixed Stack",
          "chain": [
            {"uid": "i1", "type": "pedal", "toneId": "pedal-ok", "title": "OK Pedal",
             "format": "nam", "gearTag": "pedal", "bypassed": false,
             "channels": [], "activeChannel": 0},
            {"uid": "i2", "type": "pedal", "toneId": "pedal-bad", "title": "Bad Pedal",
             "format": "nam", "gearTag": "pedal", "bypassed": "not-a-bool",
             "channels": [], "activeChannel": 0}
          ]
        },
        {
          "name": "Good Stack",
          "chain": [
            {"uid": "i1", "type": "amp", "toneId": "amp-1", "title": "Amp One",
             "format": "nam", "gearTag": "amp", "bypassed": false,
             "channels": [{"toneId": "amp-1", "title": "Amp One"}], "activeChannel": 0}
          ]
        }
      ]
    }
    )JSON";

    auto stacks = StackModel::parse(fixture);
    REQUIRE(stacks.size() == 2);
    REQUIRE(stacks[0].name == "Mixed Stack");
    REQUIRE(stacks[0].chain.size() == 1);   // the malformed item was skipped
    REQUIRE(stacks[0].chain[0].toneId == "pedal-ok");
    REQUIRE(stacks[1].name == "Good Stack");
    REQUIRE(stacks[1].chain.size() == 1);
    REQUIRE(stacks[1].chain[0].toneId == "amp-1");
}

TEST_CASE(
    "StackModel parse v2: a stack with a malformed top-level field is dropped, others survive") {
    // stack[0]'s "uid" is a number (should be a string) -- the reviewer's
    // originally-reported bug: without per-stack isolation this used to
    // wipe out EVERY stack in the file, not just this one.
    static const char* fixture = R"JSON(
    {
      "version": 2,
      "stacks": [
        {
          "name": "Broken Stack",
          "uid": 12345,
          "chain": [
            {"uid": "i1", "type": "pedal", "toneId": "pedal-1", "title": "P",
             "format": "nam", "gearTag": "pedal", "bypassed": false,
             "channels": [], "activeChannel": 0}
          ]
        },
        {
          "name": "Good Stack",
          "chain": []
        }
      ]
    }
    )JSON";

    auto stacks = StackModel::parse(fixture);
    REQUIRE(stacks.size() == 1);
    REQUIRE(stacks[0].name == "Good Stack");
}

TEST_CASE("StackModel v1 migration: a malformed slot is skipped, its stack and others survive") {
    // stack[0]'s AMP slot has a numeric "id" (should be a string) -- only
    // that ONE slot should be dropped, not the whole stack or the file.
    static const char* fixture = R"JSON(
    [
      {
        "name": "Mixed Rig",
        "slots": [
          {"id": 12345, "title": "Bad Amp", "format": "nam"},
          {"id": "cab-y", "title": "Cab Y", "format": "ir"},
          {"id": "", "title": "", "format": ""},
          {"id": "", "title": "", "format": ""},
          {"id": "", "title": "", "format": ""},
          {"id": "", "title": "", "format": ""}
        ]
      },
      {
        "name": "Good Rig",
        "slots": [
          {"id": "amp-z", "title": "Amp Z", "format": "nam"},
          {"id": "", "title": "", "format": ""},
          {"id": "", "title": "", "format": ""},
          {"id": "", "title": "", "format": ""},
          {"id": "", "title": "", "format": ""},
          {"id": "", "title": "", "format": ""}
        ]
      }
    ]
    )JSON";

    auto stacks = StackModel::parse(fixture);
    REQUIRE(stacks.size() == 2);
    REQUIRE(stacks[0].name == "Mixed Rig");
    REQUIRE(stacks[0].chain.size() == 1);   // the malformed AMP slot was skipped
    REQUIRE(stacks[0].chain[0].toneId == "cab-y");
    REQUIRE(stacks[0].chain[0].type == GearType::Cab);
    REQUIRE(stacks[1].name == "Good Rig");
    REQUIRE(stacks[1].chain.size() == 1);
    REQUIRE(stacks[1].chain[0].toneId == "amp-z");
    // Both v1-migrated stacks get unique, monotonic stack uids.
    REQUIRE(stacks[0].uid == "s1");
    REQUIRE(stacks[1].uid == "s2");
}

TEST_CASE(
    "StackModel v1 migration: a stack with a malformed name field is dropped, others survive") {
    static const char* fixture = R"JSON(
    [
      {
        "name": 42,
        "slots": [
          {"id": "amp-x", "title": "Amp X", "format": "nam"}
        ]
      },
      {
        "name": "Good Rig",
        "slots": [
          {"id": "amp-z", "title": "Amp Z", "format": "nam"}
        ]
      }
    ]
    )JSON";

    auto stacks = StackModel::parse(fixture);
    REQUIRE(stacks.size() == 1);
    REQUIRE(stacks[0].name == "Good Rig");
}

TEST_CASE("StackModel parse mints stack uids when absent, unique within the file") {
    static const char* fixture = R"JSON(
    {
      "version": 2,
      "stacks": [
        {"name": "No Uid A", "chain": []},
        {"name": "Has Uid", "uid": "s5", "chain": []},
        {"name": "No Uid B", "chain": []}
      ]
    }
    )JSON";
    auto stacks = StackModel::parse(fixture);
    REQUIRE(stacks.size() == 3);
    REQUIRE(stacks[1].uid == "s5");   // explicit uid left untouched
    REQUIRE_FALSE(stacks[0].uid.empty());
    REQUIRE_FALSE(stacks[2].uid.empty());
    REQUIRE(stacks[0].uid != stacks[1].uid);
    REQUIRE(stacks[0].uid != stacks[2].uid);
    REQUIRE(stacks[1].uid != stacks[2].uid);
    // Auto-minted uids must not collide with the explicit "s5" already
    // present -- same monotonic-vs-highest-existing-index convention as
    // ChainItem uids (nextUid).
    auto idx = [](const std::string& uid) { return std::stoi(uid.substr(1)); };
    REQUIRE(idx(stacks[0].uid) > 5);
    REQUIRE(idx(stacks[2].uid) > 5);
}

TEST_CASE("StackModel parse mints fresh uids for hand-edited duplicate stack uids") {
    // Adversarial-review finding on the Stack.uid commit: two stacks sharing
    // an explicit uid (only reachable via a hand-edited file) would let
    // AppShellStackApply.cpp's (index,uid) revalidation accept the WRONG
    // stack once one of them shifts index. Only the first occurrence is
    // kept as-is; every later duplicate is re-minted fresh.
    static const char* fixture = R"JSON(
    {
      "version": 2,
      "stacks": [
        {"name": "A", "uid": "s2", "chain": []},
        {"name": "B", "uid": "s2", "chain": []}
      ]
    }
    )JSON";
    auto stacks = StackModel::parse(fixture);
    REQUIRE(stacks.size() == 2);
    REQUIRE(stacks[0].uid == "s2");
    REQUIRE(stacks[1].uid != "s2");
    REQUIRE_FALSE(stacks[1].uid.empty());
}

TEST_CASE("StackModel nextStackUid does not overflow on an adversarial max-int uid") {
    // Adversarial-review finding: a hand-edited uid of exactly "s2147483647"
    // (INT_MAX) parses via stoi without throwing, so an unguarded maxN + 1
    // is signed-integer overflow (UB). The exact recovered value isn't
    // load-bearing, only that minting stays well-formed and doesn't collide
    // with the adversarial input.
    std::vector<Stack> stacks;
    Stack a;
    a.uid = "s2147483647";
    stacks.push_back(a);
    const auto uid = StackModel::nextStackUid(stacks);
    REQUIRE(uid != "s2147483647");
    REQUIRE(uid.rfind("s", 0) == 0);
}

TEST_CASE("StackModel looksLikeStacksFile distinguishes unrecognized content from a valid, "
          "possibly-empty file") {
    // Backs AppShell::loadStacksState's backup trigger: a legitimately
    // empty v1/v2 file (e.g. the user deleted their last rig) must NOT
    // trigger a backup, only content parse() couldn't make sense of at all.
    REQUIRE(StackModel::looksLikeStacksFile(R"({"version":2,"stacks":[]})"));
    REQUIRE(StackModel::looksLikeStacksFile("[]"));   // empty v1 array
    REQUIRE(StackModel::looksLikeStacksFile(R"([{"name":"Old","slots":[]}])"));
    REQUIRE_FALSE(StackModel::looksLikeStacksFile("not valid json {{{"));
    REQUIRE_FALSE(StackModel::looksLikeStacksFile(""));
    REQUIRE_FALSE(StackModel::looksLikeStacksFile("null"));
    REQUIRE_FALSE(StackModel::looksLikeStacksFile(R"({"version":1,"stacks":[]})"));
    REQUIRE_FALSE(StackModel::looksLikeStacksFile(R"({"foo":"bar"})"));
}

TEST_CASE("StackModel nextStackUid is monotonic based on the highest existing stack uid") {
    std::vector<Stack> stacks;
    REQUIRE(StackModel::nextStackUid(stacks) == "s1");

    Stack a;
    a.uid = "s1";
    stacks.push_back(a);
    REQUIRE(StackModel::nextStackUid(stacks) == "s2");

    Stack b;
    b.uid = "s5";
    stacks.push_back(b);
    REQUIRE(StackModel::nextStackUid(stacks) == "s6");

    Stack c;
    c.uid = "s3";
    stacks.push_back(c);
    REQUIRE(StackModel::nextStackUid(stacks) == "s6");   // still based on max (s5), not count
}

TEST_CASE("StackModel serialize never throws on invalid UTF-8 in a string field") {
    Stack st;
    st.name = "Good Name";
    ChainItem it;
    it.uid = "i1";
    it.type = GearType::Pedal;
    it.toneId = "t1";
    // A lone continuation byte (0x80) is never valid UTF-8 on its own.
    it.title = std::string("Invalid \x80\x81 UTF8");
    st.chain.push_back(it);
    std::vector<Stack> stacks{ st };

    std::string out;
    REQUIRE_NOTHROW(out = StackModel::serialize(stacks));
    REQUIRE_FALSE(out.empty());

    // The rest of the payload stays valid, parseable JSON.
    auto reparsed = StackModel::parse(out);
    REQUIRE(reparsed.size() == 1);
    REQUIRE(reparsed[0].name == "Good Name");
}
