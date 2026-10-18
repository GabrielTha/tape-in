// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/session/dmap.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace {

using tapein::core::session::parseDmapMetadata;
using tapein::core::session::parseProgress;

// Appends a DMAP tag: 4-char code, u32 big-endian length, payload bytes.
void appendTag(std::vector<uint8_t>& out, const char (&code)[5], const std::vector<uint8_t>& payload)
{
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<uint8_t>(code[i]));
    const auto n = static_cast<uint32_t>(payload.size());
    out.push_back(static_cast<uint8_t>((n >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((n >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((n >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(n & 0xFF));
    out.insert(out.end(), payload.begin(), payload.end());
}

std::vector<uint8_t> str(std::string_view s) { return {s.begin(), s.end()}; }

std::span<const uint8_t> asSpan(const std::vector<uint8_t>& v) { return {v.data(), v.size()}; }

} // namespace

TEST_CASE("parseDmapMetadata reads minm/asar/asal from a flat blob")
{
    std::vector<uint8_t> body;
    appendTag(body, "minm", str("Midnight City"));
    appendTag(body, "asar", str("M83"));
    appendTag(body, "asal", str("Hurry Up, We're Dreaming"));

    const auto md = parseDmapMetadata(asSpan(body));
    CHECK(md.trackName == "Midnight City");
    CHECK(md.artist == "M83");
    CHECK(md.album == "Hurry Up, We're Dreaming");
}

TEST_CASE("parseDmapMetadata recurses one level into mlit")
{
    std::vector<uint8_t> inner;
    appendTag(inner, "minm", str("Weightless"));
    appendTag(inner, "asar", str("Marconi Union"));
    std::vector<uint8_t> body;
    appendTag(body, "mlit", inner);

    const auto md = parseDmapMetadata(asSpan(body));
    CHECK(md.trackName == "Weightless");
    CHECK(md.artist == "Marconi Union");
}

TEST_CASE("parseDmapMetadata ignores a tag whose length overruns the buffer")
{
    // 'minm' declares 0x40 (64) bytes but only 2 follow.
    std::vector<uint8_t> body = {'m', 'i', 'n', 'm', 0x00, 0x00, 0x00, 0x40, 'h', 'i'};
    const auto md = parseDmapMetadata(asSpan(body));
    CHECK(md.trackName.empty());
}

TEST_CASE("parseDmapMetadata clamps an overlong name to 256 bytes")
{
    std::vector<uint8_t> body;
    appendTag(body, "minm", str(std::string(1000, 'a')));
    const auto md = parseDmapMetadata(asSpan(body));
    CHECK(md.trackName.size() <= 256);
}

TEST_CASE("parseDmapMetadata tolerates an empty body")
{
    const auto md = parseDmapMetadata({});
    CHECK(md.trackName.empty());
    CHECK(md.artist.empty());
    CHECK(md.album.empty());
}

TEST_CASE("parseProgress returns the three frame counts")
{
    const auto p = parseProgress("progress: 1/44101/176400");
    REQUIRE(p.has_value());
    CHECK(p->startFrames == 1u);
    CHECK(p->currentFrames == 44101u);
    CHECK(p->endFrames == 176400u);
}

TEST_CASE("parseProgress accepts the bare three-number form")
{
    const auto p = parseProgress("1/44101/176400");
    REQUIRE(p.has_value());
    CHECK(p->currentFrames == 44101u);
}

TEST_CASE("parseProgress rejects a malformed line")
{
    CHECK_FALSE(parseProgress("progress: 1/2").has_value());
    CHECK_FALSE(parseProgress("progress: a/b/c").has_value());
    CHECK_FALSE(parseProgress("").has_value());
}
