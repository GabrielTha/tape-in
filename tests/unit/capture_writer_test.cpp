// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/cli/capture_writer.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace tapein;

namespace {
std::vector<uint8_t> readAll(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
} // namespace

TEST_CASE("capture writes length-prefixed datagrams")
{
    const auto dir = std::filesystem::temp_directory_path() / "tapein_capture_test";
    std::filesystem::remove_all(dir);
    cli::CaptureWriter capture(dir); // creates the directory

    const std::vector<uint8_t> a{0x80, 0x60, 1, 2, 3};
    const std::vector<uint8_t> b{0x80, 0xD4, 9};
    capture.rtp(7, cli::CaptureWriter::Socket::audio, a);
    capture.rtp(7, cli::CaptureWriter::Socket::control, b);
    capture.close(7);

    const auto bytes = readAll(dir / "rtp-7.bin");
    // [kind:1][len:4 big-endian][payload] per record
    REQUIRE(bytes.size() == (5 + 5) + (5 + 3));
    REQUIRE(bytes[0] == 0); // audio
    REQUIRE(bytes[4] == 5);
    REQUIRE(bytes[5] == 0x80);
    REQUIRE(bytes[9] == 3);
    REQUIRE(bytes[10] == 1); // control
    REQUIRE(bytes[14] == 3);
    REQUIRE(bytes[17] == 9);
    std::filesystem::remove_all(dir);
}

TEST_CASE("capture writes RTSP traffic as text with direction markers")
{
    const auto dir = std::filesystem::temp_directory_path() / "tapein_capture_test2";
    std::filesystem::remove_all(dir);
    cli::CaptureWriter capture(dir);
    const std::string in = "OPTIONS * RTSP/1.0\r\nCSeq: 1\r\n\r\n";
    const std::string out = "RTSP/1.0 200 OK\r\nCSeq: 1\r\n\r\n";
    capture.rtsp(3, false, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(in.data()), in.size()));
    capture.rtsp(3, true, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(out.data()), out.size()));
    capture.closeAll();
    const auto bytes = readAll(dir / "rtsp-3.log");
    const std::string text(bytes.begin(), bytes.end());
    REQUIRE(text.find("<<< ") != std::string::npos);
    REQUIRE(text.find(">>> ") != std::string::npos);
    REQUIRE(text.find("OPTIONS * RTSP/1.0") != std::string::npos);
    REQUIRE(text.find("RTSP/1.0 200 OK") != std::string::npos);
    std::filesystem::remove_all(dir);
}
