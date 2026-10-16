// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/cli/wav_sink.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <vector>

using namespace tapein;

TEST_CASE("WavSink writes one file per session named after the client")
{
    const auto dir = std::filesystem::temp_directory_path() / "tapein_wav_sink_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    std::vector<std::string> logs;
    cli::WavSink sink(dir, [&](std::string m) { logs.push_back(std::move(m)); });

    core::session::SessionInfo info;
    info.id = 7;
    info.clientName = "Test iPhone/2";
    sink.onSessionStarted(info);
    REQUIRE(sink.files().size() == 1);
    REQUIRE(sink.files()[0].filename().string().find("-Test_iPhone_2-7.wav") != std::string::npos);

    std::vector<int16_t> pcm(352 * 2, 5);
    core::buffer::ReleasedBlock block{0, 0, false, pcm};
    sink.onPcm(7, block);
    sink.onPcm(7, block);
    sink.onPcm(99, block); // unknown session: ignored

    core::session::SessionStats stats;
    stats.packetsReceived = 2;
    sink.onSessionEnded(7, "teardown", stats);

    REQUIRE(std::filesystem::file_size(sink.files()[0]) == 44 + 2 * 352 * 2 * 2);
    REQUIRE(logs.size() == 2);
    REQUIRE(logs[1].find("704 frames") != std::string::npos);
    REQUIRE(logs[1].find("packets 2") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("WavSink sanitize")
{
    REQUIRE(cli::WavSink::sanitize("Gabriel's iPhone") == "Gabriel_s_iPhone");
    REQUIRE(cli::WavSink::sanitize("") == "unknown");
    REQUIRE(cli::WavSink::sanitize("ok-name_1.2") == "ok-name_1.2");
}
