// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/util/wav_writer.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using tapein::core::util::WavWriter;

namespace {

std::vector<uint8_t> readAll(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

uint32_t le32(const std::vector<uint8_t>& b, size_t at)
{
    return static_cast<uint32_t>(b[at]) | (static_cast<uint32_t>(b[at + 1]) << 8) | (static_cast<uint32_t>(b[at + 2]) << 16)
           | (static_cast<uint32_t>(b[at + 3]) << 24);
}

uint16_t le16(const std::vector<uint8_t>& b, size_t at)
{
    return static_cast<uint16_t>(b[at] | (b[at + 1] << 8));
}

} // namespace

TEST_CASE("WavWriter writes a valid header with correct sizes")
{
    const auto path = std::filesystem::temp_directory_path() / "tapein_wav_writer_test.wav";
    std::filesystem::remove(path);
    {
        auto w = WavWriter::open(path, 44100, 2);
        REQUIRE(w.ok());
        std::vector<int16_t> frames{1, -1, 2, -2, 3, -3}; // 3 stereo frames
        REQUIRE(w.value().write(frames).ok());
        REQUIRE(w.value().write(frames).ok());
        REQUIRE(w.value().framesWritten() == 6);
        REQUIRE(w.value().close().ok());
    }
    const auto bytes = readAll(path);
    REQUIRE(bytes.size() == 44 + 24);
    REQUIRE(std::string(bytes.begin(), bytes.begin() + 4) == "RIFF");
    REQUIRE(le32(bytes, 4) == 36 + 24);
    REQUIRE(std::string(bytes.begin() + 8, bytes.begin() + 12) == "WAVE");
    REQUIRE(std::string(bytes.begin() + 12, bytes.begin() + 16) == "fmt ");
    REQUIRE(le32(bytes, 16) == 16);
    REQUIRE(le16(bytes, 20) == 1); // PCM
    REQUIRE(le16(bytes, 22) == 2); // channels
    REQUIRE(le32(bytes, 24) == 44100);
    REQUIRE(le32(bytes, 28) == 44100 * 4); // byte rate
    REQUIRE(le16(bytes, 32) == 4); // block align
    REQUIRE(le16(bytes, 34) == 16); // bits
    REQUIRE(std::string(bytes.begin() + 36, bytes.begin() + 40) == "data");
    REQUIRE(le32(bytes, 40) == 24);
    REQUIRE(le16(bytes, 44) == 1);
    REQUIRE(static_cast<int16_t>(le16(bytes, 46)) == -1);
    std::filesystem::remove(path);
}

TEST_CASE("WavWriter destructor finalises the header")
{
    const auto path = std::filesystem::temp_directory_path() / "tapein_wav_writer_dtor.wav";
    std::filesystem::remove(path);
    {
        auto w = WavWriter::open(path, 44100, 2);
        REQUIRE(w.ok());
        std::vector<int16_t> frames(4, 0);
        REQUIRE(w.value().write(frames).ok());
    }
    const auto bytes = readAll(path);
    REQUIRE(le32(bytes, 40) == 8);
    std::filesystem::remove(path);
}

TEST_CASE("WavWriter rejects an unwritable path")
{
    auto w = WavWriter::open(std::filesystem::path("/nonexistent_dir_tapein/x.wav"), 44100, 2);
    REQUIRE_FALSE(w.ok());
}
