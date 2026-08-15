#include <catch2/catch_all.hpp>
#include "json.hpp"
#include "model/StackModel.h"

using namespace nam;
using json = nlohmann::json;

TEST_CASE("StackModel v2 round-trip preserves every field incl. unknown keys") {
    static const char* v2Fixture = R"JSON(
    {
      "version": 2,
      "stacks": [
        {
          "name": "My Rig",
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
    REQUIRE(st.routing == Stack::Routing::AB);
    REQUIRE(st.activeScene == 0);
    REQUIRE(st.chain.size() == 1);

    const auto& item = st.chain[0];
    REQUIRE(item.uid == "i1");
    REQUIRE(item.type == GearType::Amp);
    REQUIRE(item.toneId == "amp-1");
    REQUIRE(item.title == "Amp One");
    REQUIRE(item.format == "nam");
    REQUIRE(item.gearTag == "amp");
    REQUIRE(item.fs == 3);
    REQUIRE_FALSE(item.bypassed);
    REQUIRE(item.channels.size() == 2);
    REQUIRE(item.channels[1].toneId == "amp-1b");
    REQUIRE(item.activeChannel == 1);

    REQUIRE(st.scenes.size() == 1);
    REQUIRE(st.scenes[0].name == "Clean");
    REQUIRE(st.scenes[0].pedalBypass.at("i2") == true);

    auto out = StackModel::serialize(stacks);
    json reparsed = json::parse(out);
    REQUIRE(reparsed["version"] == 2);
    // Unknown keys on both stack and item objects survive the round trip.
    REQUIRE(reparsed["stacks"][0]["notes"] == "custom-stack-field");
    REQUIRE(reparsed["stacks"][0]["chain"][0]["vendorExtra"] == 42);

    auto stacks2 = StackModel::parse(out);
    REQUIRE(stacks2.size() == 1);
    REQUIRE(stacks2[0].chain[0].activeChannel == 1);
    REQUIRE(stacks2[0].chain[0].channels.size() == 2);
    REQUIRE(stacks2[0].routing == Stack::Routing::AB);
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
    REQUIRE(st.chain[3].gearTag == "spaces");
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

TEST_CASE("StackModel sceneApplyPlan returns channel toneId + bypass pairs, clamps bad indices") {
    Stack s;
    ChainItem amp;
    amp.uid = "i1";
    amp.type = GearType::Amp;
    amp.channels = { { "clean-tone", "Clean" }, { "lead-tone", "Lead" } };
    amp.activeChannel = 0;
    s.chain.push_back(amp);

    ChainItem pedal;
    pedal.uid = "i2";
    pedal.type = GearType::Pedal;
    s.chain.push_back(pedal);

    Scene sc;
    sc.name = "Lead";
    sc.ampChannel = 1;
    sc.pedalBypass = { { "i2", true } };
    s.scenes.push_back(sc);

    auto plan = StackModel::sceneApplyPlan(s, 0);
    REQUIRE(plan.modelToneId == "lead-tone");
    REQUIRE(plan.modelTitle == "Lead");
    REQUIRE(plan.bypass.size() == 1);
    REQUIRE(plan.bypass[0].first == "i2");
    REQUIRE(plan.bypass[0].second == true);

    // Out-of-range scene index clamps to an empty plan.
    auto badPlan = StackModel::sceneApplyPlan(s, 5);
    REQUIRE(badPlan.modelToneId.empty());
    REQUIRE(badPlan.modelTitle.empty());
    REQUIRE(badPlan.bypass.empty());

    auto negPlan = StackModel::sceneApplyPlan(s, -1);
    REQUIRE(negPlan.modelToneId.empty());
    REQUIRE(negPlan.bypass.empty());
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
