// tests/test_irloader.cpp
#include <catch2/catch_all.hpp>
#include <cmath>
#include <filesystem>
#include <memory>
#include "model/IrLoader.h"

// Writes a minimal 48k mono 32-bit-float WAV with the given samples.
static std::string writeWav(const std::string& name, const std::vector<float>& s, int sr);

TEST_CASE("IrLoader returns nullptr on bad path") {
    auto ir = nam::loadImpulseResponse("nope.wav", 48000, 4096);
    REQUIRE(ir == nullptr);
}

TEST_CASE("IrLoader loads a mono 48k WAV unchanged") {
    std::vector<float> s{0.0f, 1.0f, -0.5f, 0.25f};
    auto path = writeWav("ir_test_48k.wav", s, 48000);
    auto ir = nam::loadImpulseResponse(path, 48000, 4096);
    std::filesystem::remove(path);
    REQUIRE(ir != nullptr);
    REQUIRE(ir->size() == 4u);
    REQUIRE((*ir)[1] == Catch::Approx(1.0f).margin(1e-4));
}

TEST_CASE("IrLoader truncates to maxTaps") {
    std::vector<float> s(10000, 0.1f);
    auto path = writeWav("ir_test_long.wav", s, 48000);
    auto ir = nam::loadImpulseResponse(path, 48000, 4096);
    std::filesystem::remove(path);
    REQUIRE(ir != nullptr);
    REQUIRE((int) ir->size() == 4096);
}

TEST_CASE("IrLoader downsamples 96k to 48k") {
    const int N = 2000;
    std::vector<float> s(N);
    for (int i = 0; i < N; ++i) s[(size_t) i] = std::sin(0.05f * (float) i);
    auto path = writeWav("ir_test_96k.wav", s, 96000);
    auto ir = nam::loadImpulseResponse(path, 48000, 1'000'000);
    std::filesystem::remove(path);
    REQUIRE(ir != nullptr);
    for (float v : *ir) REQUIRE(std::isfinite(v));
    const int expected = N / 2;
    REQUIRE((int) ir->size() >= expected - 2);
    REQUIRE((int) ir->size() <= expected + 2);
}

TEST_CASE("IrLoader upsamples 44100 to 48000") {
    const int N = 2000;
    std::vector<float> s(N);
    for (int i = 0; i < N; ++i) s[(size_t) i] = std::sin(0.05f * (float) i);
    auto path = writeWav("ir_test_44100.wav", s, 44100);
    auto ir = nam::loadImpulseResponse(path, 48000, 1'000'000);
    std::filesystem::remove(path);
    REQUIRE(ir != nullptr);
    for (float v : *ir) REQUIRE(std::isfinite(v));
    const int expected = (int) ((double) N * 48000.0 / 44100.0);
    REQUIRE((int) ir->size() >= expected - 2);
    REQUIRE((int) ir->size() <= expected + 2);
}

TEST_CASE("IrLoader guards invalid maxTaps/targetSampleRate without throwing") {
    std::vector<float> s{0.0f, 1.0f, -0.5f, 0.25f};
    auto path = writeWav("ir_test_guard.wav", s, 48000);

    std::shared_ptr<const std::vector<float>> ir1;
    REQUIRE_NOTHROW(ir1 = nam::loadImpulseResponse(path, 48000, -1));
    REQUIRE(ir1 == nullptr);

    std::shared_ptr<const std::vector<float>> ir2;
    REQUIRE_NOTHROW(ir2 = nam::loadImpulseResponse(path, 0, 4096));
    REQUIRE(ir2 == nullptr);

    std::filesystem::remove(path);
}

#include "dr_wav.h"
static std::string writeWav(const std::string& name, const std::vector<float>& s, int sr) {
    auto path = (std::filesystem::temp_directory_path() / name).string();
    drwav_data_format fmt{};
    fmt.container = drwav_container_riff; fmt.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    fmt.channels = 1; fmt.sampleRate = (drwav_uint32) sr; fmt.bitsPerSample = 32;
    drwav wav;
    drwav_init_file_write(&wav, path.c_str(), &fmt, nullptr);
    drwav_write_pcm_frames(&wav, s.size(), s.data());
    drwav_uninit(&wav);
    return path;
}
