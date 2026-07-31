#include <catch2/catch_all.hpp>
#include <future>
#include <chrono>
#include "model/ModelHost.h"

TEST_CASE("ModelHost loadNow succeeds on valid file") {
    nam::ModelHost host; host.configure(48000, 128);
    std::shared_ptr<nam::NamModel> got;
    host.loadNow(NAM_FIXTURE_A2, [&](auto m){ got = m; });
    REQUIRE(got != nullptr);
    REQUIRE(got->sampleRate() == 48000);
}

TEST_CASE("ModelHost loadNow reports failure as nullptr") {
    nam::ModelHost host; host.configure(48000, 128);
    bool called = false; std::shared_ptr<nam::NamModel> got;
    host.loadNow("nope.nam", [&](auto m){ called = true; got = m; });
    REQUIRE(called);
    REQUIRE(got == nullptr);
}

TEST_CASE("ModelHost requestLoad eventually calls back") {
    nam::ModelHost host; host.configure(48000, 128);
    std::promise<bool> done;
    host.requestLoad(NAM_FIXTURE_A2, [&](auto m){ done.set_value(m != nullptr); });
    auto fut = done.get_future();
    REQUIRE(fut.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
    REQUIRE(fut.get() == true);
}
