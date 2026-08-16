#include <catch2/catch_all.hpp>

#include "json.hpp"
#include "model/ControlMap.h"

using namespace nam;
using json = nlohmann::json;

namespace {

ControlSignature cc(int number, int channel = 1) { return { ControlKind::Cc, channel, number }; }

ControlEvent ev(const ControlSignature& sig, int value, std::uint32_t timeMs) {
    return { sig, value, timeMs };
}

}   // namespace

TEST_CASE("Action ids round-trip and unknown ids degrade to None") {
    for (auto a : allControlActions()) {
        REQUIRE(controlActionFromId(controlActionId(a)) == a);
        REQUIRE(std::string(controlActionLabel(a)).size() > 0);
    }
    // A binding written by a newer build must not corrupt this one.
    REQUIRE(controlActionFromId("looper.record") == ControlAction::None);
    REQUIRE(controlActionFromId("") == ControlAction::None);
}

TEST_CASE("Signature matching treats channel 0 as any") {
    REQUIRE(cc(20, 1).matches(cc(20, 1)));
    REQUIRE_FALSE(cc(20, 1).matches(cc(21, 1)));
    REQUIRE_FALSE(cc(20, 1).matches(cc(20, 2)));
    // A learned binding is channel-agnostic so changing the pedal's global
    // channel does not silently break every binding.
    REQUIRE(cc(20, 0).matches(cc(20, 7)));
    REQUIRE(cc(20, 3).matches(cc(20, 0)));
    // Kind still has to agree.
    ControlSignature note{ ControlKind::Note, 1, 20 };
    REQUIRE_FALSE(cc(20, 1).matches(note));
}

TEST_CASE("Learn binds the next pressed switch and consumes that press") {
    ControlMap m;
    m.beginLearn(ControlAction::RigNext);
    REQUIRE(m.isLearning());

    // A release-value event must not be what gets learned.
    REQUIRE(m.handle(ev(cc(20), 0, 0)) == ControlAction::None);
    REQUIRE(m.isLearning());

    // The press binds -- and does NOT also fire, so assigning a switch never
    // triggers whatever it used to do.
    REQUIRE(m.handle(ev(cc(20), 127, 10)) == ControlAction::None);
    REQUIRE_FALSE(m.isLearning());

    const auto* b = m.bindingFor(ControlAction::RigNext);
    REQUIRE(b != nullptr);
    REQUIRE(b->sig.number == 20);
}

TEST_CASE("Bindings are 1:1 in both directions") {
    ControlMap m;
    m.bind(cc(20), ControlAction::RigNext);
    m.bind(cc(21), ControlAction::RigPrev);
    REQUIRE(m.bindings().size() == 2);

    // Re-learning an action MOVES it rather than leaving a stale duplicate
    // that would double-fire.
    m.bind(cc(22), ControlAction::RigNext);
    REQUIRE(m.bindings().size() == 2);
    REQUIRE(m.bindingFor(ControlAction::RigNext)->sig.number == 22);

    // Reusing a signature reassigns that switch rather than stacking two
    // actions onto one stomp.
    m.bind(cc(21), ControlAction::TunerToggle);
    REQUIRE(m.bindings().size() == 2);
    REQUIRE(m.bindingFor(ControlAction::RigPrev) == nullptr);
    REQUIRE(m.bindingFor(ControlAction::TunerToggle)->sig.number == 21);

    m.unbind(ControlAction::TunerToggle);
    REQUIRE(m.bindingFor(ControlAction::TunerToggle) == nullptr);
}

TEST_CASE("Unbound traffic is ignored") {
    ControlMap m;
    m.bind(cc(20), ControlAction::RigNext);
    REQUIRE(m.handle(ev(cc(99), 127, 0)) == ControlAction::None);
}

TEST_CASE("JSON round-trip preserves bindings and unknown keys") {
    ControlMap m;
    m.bind(cc(20, 1), ControlAction::RigNext, FirePolicy::Momentary);
    m.bind(cc(21, 0), ControlAction::TunerToggle, FirePolicy::Toggle);

    auto j = m.toJson();
    // Simulate a newer build having written a field this build knows nothing
    // about, plus an action it does not implement.
    j["bindings"][0]["holdMs"] = 750;
    j["bindings"].push_back({ { "kind", "cc" },
                              { "channel", 1 },
                              { "number", 30 },
                              { "action", "looper.record" },
                              { "policy", "auto" } });

    auto back = ControlMap::fromJson(j);
    REQUIRE(back.bindings().size() == 3);

    const auto* next = back.bindingFor(ControlAction::RigNext);
    REQUIRE(next != nullptr);
    REQUIRE(next->sig.number == 20);
    REQUIRE(next->sig.channel == 1);
    REQUIRE(next->policy == FirePolicy::Momentary);

    const auto* tuner = back.bindingFor(ControlAction::TunerToggle);
    REQUIRE(tuner != nullptr);
    REQUIRE(tuner->sig.channel == 0);
    REQUIRE(tuner->policy == FirePolicy::Toggle);

    // The unknown action parses to None (inert) but its row survives, and
    // the unknown key rides along untouched.
    auto j2 = back.toJson();
    REQUIRE(j2["bindings"].size() == 3);
    REQUIRE(j2["bindings"][0]["holdMs"] == 750);

    bool foundUnknown = false;
    for (const auto& e : j2["bindings"])
        if (e.value("number", 0) == 30) {
            foundUnknown = true;
            // Round-tripping must not silently rewrite it to something else.
            REQUIRE(e.value("action", std::string()) == "");
        }
    REQUIRE(foundUnknown);
}

// ---------------------------------------------------------------------------
// shouldFire -- the fire-decision spec. RED until ControlMap.cpp's TODO is
// implemented. Each case is one physical stomp sequence from a real pedal.
// ---------------------------------------------------------------------------

TEST_CASE("Momentary policy fires on press only") {
    ControlBinding b{ cc(20), ControlAction::RigNext, FirePolicy::Momentary };
    FireState st;
    REQUIRE(shouldFire(b, ev(cc(20), 127, 0), st));
    REQUIRE_FALSE(shouldFire(b, ev(cc(20), 0, 40), st));
    REQUIRE(shouldFire(b, ev(cc(20), 127, 900), st));
    REQUIRE_FALSE(shouldFire(b, ev(cc(20), 0, 940), st));
}

TEST_CASE("Toggle policy fires on every event") {
    ControlBinding b{ cc(20), ControlAction::RigNext, FirePolicy::Toggle };
    FireState st;
    // A toggle pedal alternates 127/0, but each event IS one press.
    REQUIRE(shouldFire(b, ev(cc(20), 127, 0), st));
    REQUIRE(shouldFire(b, ev(cc(20), 0, 3000), st));
    REQUIRE(shouldFire(b, ev(cc(20), 127, 6000), st));
}

TEST_CASE("Auto handles a momentary pedal: one fire per stomp") {
    ControlBinding b{ cc(20), ControlAction::RigNext, FirePolicy::Auto };
    FireState st;
    // First stomp must respond -- there is no history to infer from yet.
    REQUIRE(shouldFire(b, ev(cc(20), 127, 0), st));
    // The release lands within the window: it is the foot lifting.
    REQUIRE_FALSE(shouldFire(b, ev(cc(20), 0, 35), st));
    REQUIRE(shouldFire(b, ev(cc(20), 127, 2000), st));
    REQUIRE_FALSE(shouldFire(b, ev(cc(20), 0, 2030), st));
}

TEST_CASE("Auto handles a toggle pedal: one fire per press") {
    ControlBinding b{ cc(21), ControlAction::RigPrev, FirePolicy::Auto };
    FireState st;
    REQUIRE(shouldFire(b, ev(cc(21), 127, 0), st));
    // Seconds later -- far outside the release window, so this 0 is a fresh
    // press, not a release, and dropping it would cost the user a stomp.
    REQUIRE(shouldFire(b, ev(cc(21), 0, 4000), st));
    REQUIRE(shouldFire(b, ev(cc(21), 127, 8000), st));
}

TEST_CASE("Auto latches momentary, so a long-held switch stops double-firing") {
    // The guess-toggle rule alone would fire on the release of any momentary
    // press held longer than the window -- i.e. every time the user rests a
    // foot on the switch. One confirmed fast release is enough to classify
    // the switch for good.
    ControlBinding b{ cc(22), ControlAction::TunerToggle, FirePolicy::Auto };
    FireState st;

    // Stomp 1: a normal quick press teaches Auto that this switch is
    // momentary.
    REQUIRE(shouldFire(b, ev(cc(22), 127, 0), st));
    REQUIRE_FALSE(shouldFire(b, ev(cc(22), 0, 30), st));
    REQUIRE(st.sawRelease);

    // Stomp 2: held for two seconds. Without the latch the release would be
    // read as a fresh toggle press and fire a second time.
    REQUIRE(shouldFire(b, ev(cc(22), 127, 5000), st));
    REQUIRE_FALSE(shouldFire(b, ev(cc(22), 0, 7000), st));
}

TEST_CASE("Auto fires when the very first event of a switch is low") {
    // A toggle pedal can come up in its "off" half, so the user's first
    // stomp arrives as a 0. Swallowing it would look like a dead switch.
    ControlBinding b{ cc(23), ControlAction::ChainBypass, FirePolicy::Auto };
    FireState st;
    REQUIRE(shouldFire(b, ev(cc(23), 0, 0), st));
}

TEST_CASE("ControlMap dispatches a bound momentary stomp exactly once") {
    ControlMap m;
    m.bind(cc(20), ControlAction::RigNext, FirePolicy::Momentary);
    REQUIRE(m.handle(ev(cc(20), 127, 0)) == ControlAction::RigNext);
    REQUIRE(m.handle(ev(cc(20), 0, 40)) == ControlAction::None);
}
