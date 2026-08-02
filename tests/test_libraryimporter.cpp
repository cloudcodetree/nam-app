#include <catch2/catch_all.hpp>
#include <filesystem>
#include <fstream>
#include "model/LibraryImporter.h"
using namespace nam;

extern int testing_counter;   // reuse the counter defined in test_librarystore.cpp
static std::string tmpLib2() {
    return (std::filesystem::temp_directory_path() /
            ("namlib_i_" + std::to_string(testing_counter++))).string();
}

#include "dr_wav.h"
static std::string writeTestWav(std::vector<float> samples, int sr) {
    auto path = (std::filesystem::temp_directory_path() /
                 ("namlib_i_wav_" + std::to_string(testing_counter++) + ".wav")).string();
    drwav_data_format fmt{};
    fmt.container = drwav_container_riff; fmt.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    fmt.channels = 1; fmt.sampleRate = (drwav_uint32) sr; fmt.bitsPerSample = 32;
    drwav wav;
    drwav_init_file_write(&wav, path.c_str(), &fmt, nullptr);
    drwav_write_pcm_frames(&wav, samples.size(), samples.data());
    drwav_uninit(&wav);
    return path;
}

TEST_CASE("importIntoLibrary copies a model and extracts architecture") {
    LibraryStore s(tmpLib2());
    auto* e = importIntoLibrary(s, NAM_MODEL_A2_REAL, LibraryType::Model, 1000);
    REQUIRE(e != nullptr);
    REQUIRE(e->type == LibraryType::Model);
    REQUIRE(e->arch == "SlimmableContainer");           // real A2 capture
    REQUIRE(std::filesystem::exists(std::filesystem::path(s.subdir(LibraryType::Model)) / e->fileName));
    REQUIRE(e->addedAt == 1000);
}

TEST_CASE("importIntoLibrary copies an IR and reads its frame count") {
    LibraryStore s(tmpLib2());
    // write a small mono 48k float WAV via dr_wav (helper mirrors test_irloader)
    auto wav = writeTestWav(std::vector<float>(2048, 0.1f), 48000);
    auto* e = importIntoLibrary(s, wav, LibraryType::Ir, 2000);
    std::filesystem::remove(wav);
    REQUIRE(e != nullptr);
    REQUIRE(e->type == LibraryType::Ir);
    REQUIRE(e->frames == 2048);
    REQUIRE(e->sampleRate == 48000);
}

TEST_CASE("importIntoLibrary returns nullptr on missing source") {
    LibraryStore s(tmpLib2());
    REQUIRE(importIntoLibrary(s, "nope.nam", LibraryType::Model, 3000) == nullptr);
}

TEST_CASE("importIntoLibrary makes filenames unique on collision") {
    LibraryStore s(tmpLib2());
    auto* a = importIntoLibrary(s, NAM_MODEL_A2_REAL, LibraryType::Model, 1);
    auto* b = importIntoLibrary(s, NAM_MODEL_A2_REAL, LibraryType::Model, 2);
    REQUIRE(a != nullptr); REQUIRE(b != nullptr);
    REQUIRE(a->fileName != b->fileName);   // second copy gets a distinct name
}
