#include <catch2/catch_all.hpp>
#include <filesystem>
#include <fstream>
#include "model/LibraryStore.h"
using namespace nam;

// simple unique counter (no rng/clock); defined here, extern'd by later tasks' tests.
int testing_counter = 0;

static std::string tmpLib() {
    auto p = std::filesystem::temp_directory_path() /
             ("namlib_" + std::to_string(::testing_counter++));
    return p.string();
}

static LibraryEntry model(const std::string& id, const std::string& name) {
    LibraryEntry e; e.id = id; e.type = LibraryType::Model;
    e.displayName = name; e.fileName = id; return e;
}

TEST_CASE("LibraryStore add/find/list round-trips") {
    LibraryStore s(tmpLib());
    s.add(model("a.nam", "Bravo"));
    s.add(model("b.nam", "Alpha"));
    REQUIRE(s.find("a.nam") != nullptr);
    auto all = s.all(LibraryType::Model);
    REQUIRE(all.size() == 2);
    REQUIRE(all[0].displayName == "Alpha");   // sorted case-insensitively
    REQUIRE(all[1].displayName == "Bravo");
}

TEST_CASE("LibraryStore persists across save/load") {
    auto dir = tmpLib();
    {
        LibraryStore s(dir);
        auto e = model("x.nam", "X");
        e.type = LibraryType::Ir;
        e.arch = "SlimmableContainer";
        e.loudness = -26.4;
        e.fileName = "x.wav";
        e.addedAt = 1000;
        e.lastUsedAt = 2000;
        e.frames = 4410;
        e.sampleRate = 48000;
        s.add(e);
        s.setFavorite("x.nam", true);
        REQUIRE(s.save());
    }
    LibraryStore s2(dir);
    REQUIRE(s2.load());
    auto* e = s2.find("x.nam");
    REQUIRE(e != nullptr);
    REQUIRE(e->favorite == true);
    REQUIRE(e->arch == "SlimmableContainer");
    REQUIRE(e->loudness == Catch::Approx(-26.4));
    REQUIRE(e->type == LibraryType::Ir);
    REQUIRE(e->displayName == "X");
    REQUIRE(e->fileName == "x.wav");
    REQUIRE(e->addedAt == 1000);
    REQUIRE(e->lastUsedAt == 2000);
    REQUIRE(e->frames == 4410);
    REQUIRE(e->sampleRate == 48000);
}

TEST_CASE("LibraryStore favorites and recents") {
    LibraryStore s(tmpLib());
    s.add(model("a.nam","A")); s.add(model("b.nam","B")); s.add(model("c.nam","C"));
    s.setFavorite("b.nam", true);
    REQUIRE(s.favorites(LibraryType::Model).size() == 1);
    s.markUsed("a.nam", 100); s.markUsed("c.nam", 300); s.markUsed("b.nam", 200);
    auto r = s.recents(LibraryType::Model, 2);
    REQUIRE(r.size() == 2);
    REQUIRE(r[0].id == "c.nam");   // most recent first
    REQUIRE(r[1].id == "b.nam");
}

TEST_CASE("LibraryStore load with no index file is empty, not an error") {
    LibraryStore s(tmpLib());
    REQUIRE(s.load());                       // absent index -> ok
    REQUIRE(s.all(LibraryType::Model).empty());
}

TEST_CASE("LibraryStore load with wrong-shape JSON (object, not array) fails") {
    auto dir = tmpLib();
    LibraryStore s(dir);
    std::filesystem::create_directories(dir);
    { std::ofstream f(std::filesystem::path(dir) / "library.json"); f << R"({"foo":1})"; }
    REQUIRE_FALSE(s.load());
    REQUIRE(s.all(LibraryType::Model).empty());
    REQUIRE(s.all(LibraryType::Ir).empty());
}

TEST_CASE("LibraryStore load with garbage non-JSON file fails") {
    auto dir = tmpLib();
    LibraryStore s(dir);
    std::filesystem::create_directories(dir);
    { std::ofstream f(std::filesystem::path(dir) / "library.json"); f << "not valid json {{{"; }
    REQUIRE_FALSE(s.load());
    REQUIRE(s.all(LibraryType::Model).empty());
}

TEST_CASE("LibraryStore recents with negative limit returns empty") {
    LibraryStore s(tmpLib());
    s.add(model("a.nam", "A"));
    s.markUsed("a.nam", 100);
    auto r = s.recents(LibraryType::Model, -1);
    REQUIRE(r.empty());
}

TEST_CASE("LibraryStore remove deletes the file") {
    auto dir = tmpLib();
    LibraryStore s(dir);
    // create a fake model file in the models subdir
    auto path = std::filesystem::path(s.subdir(LibraryType::Model)) / "k.nam";
    std::ofstream(path) << "{}";
    s.add(model("k.nam","K"));
    REQUIRE(std::filesystem::exists(path));
    REQUIRE(s.remove("k.nam"));
    REQUIRE_FALSE(std::filesystem::exists(path));
    REQUIRE(s.find("k.nam") == nullptr);
}
