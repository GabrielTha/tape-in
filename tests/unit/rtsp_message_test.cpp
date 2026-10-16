// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/rtsp/rtsp_message.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace tapein::core::rtsp;

namespace {
std::string asString(const std::vector<uint8_t>& bytes)
{
    return std::string(bytes.begin(), bytes.end());
}
} // namespace

TEST_CASE("header lookup is case-insensitive and keeps insertion order")
{
    Headers h;
    h.set("CSeq", "3");
    h.set("Apple-Challenge", "abc");
    REQUIRE(h.size() == 2);
    REQUIRE(h.get("cseq") == "3");
    REQUIRE(h.get("APPLE-CHALLENGE") == "abc");
    REQUIRE_FALSE(h.get("Session").has_value());
    REQUIRE(h.items()[0].first == "CSeq");
    REQUIRE(h.items()[1].first == "Apple-Challenge");
}

TEST_CASE("header set replaces an existing value")
{
    Headers h;
    h.set("CSeq", "1");
    h.set("cseq", "2");
    REQUIRE(h.size() == 1);
    REQUIRE(h.get("CSeq") == "2");
}

TEST_CASE("Response serialize includes Content-Length 0 when no body")
{
    Response r;
    r.headers.set("CSeq", "7");
    r.headers.set("Server", "AirTunes/105.1");
    const std::string s = asString(r.serialize());
    REQUIRE(s == "RTSP/1.0 200 OK\r\nCSeq: 7\r\nServer: AirTunes/105.1\r\nContent-Length: 0\r\n\r\n");
}

TEST_CASE("Response serialize body bytes verbatim with status and reason")
{
    Response r;
    r.status = 453;
    r.reason = "Not Enough Bandwidth";
    r.body = {'v', 'o', 'l', 'u', 'm', 'e', ':', ' ', '-', '1', '0', '\r', '\n'};
    const std::string s = asString(r.serialize());
    REQUIRE(s == "RTSP/1.0 453 Not Enough Bandwidth\r\nContent-Length: 13\r\n\r\nvolume: -10\r\n");
}
