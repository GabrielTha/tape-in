// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/rtsp/rtsp_parser.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace tapein::core::rtsp;
using tapein::core::ErrorCode;

namespace {

std::span<const uint8_t> bytesOf(std::string_view s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

const std::string kOptions = "OPTIONS * RTSP/1.0\r\n"
                             "CSeq: 1\r\n"
                             "User-Agent: AirPlay/740.6.1\r\n"
                             "Apple-Challenge: AAECAwQFBgcICQoLDA0ODw\r\n"
                             "\r\n";

} // namespace

TEST_CASE("parses OPTIONS with CSeq and Apple-Challenge")
{
    Parser p;
    const auto r = p.feed(bytesOf(kOptions));
    REQUIRE(r.ok());
    REQUIRE(r.value().size() == 1);
    const Request& req = r.value()[0];
    REQUIRE(req.method == "OPTIONS");
    REQUIRE(req.uri == "*");
    REQUIRE(req.version == "RTSP/1.0");
    REQUIRE(req.headers.get("CSeq") == "1");
    REQUIRE(req.headers.get("apple-challenge") == "AAECAwQFBgcICQoLDA0ODw");
    REQUIRE(req.body.empty());
}

TEST_CASE("feed in 1-byte chunks yields the same request")
{
    Parser p;
    std::vector<Request> got;
    for (const char c : kOptions) {
        const uint8_t b = static_cast<uint8_t>(c);
        const auto r = p.feed(std::span<const uint8_t>(&b, 1));
        REQUIRE(r.ok());
        for (const auto& req : r.value()) {
            got.push_back(req);
        }
    }
    REQUIRE(got.size() == 1);
    REQUIRE(got[0].method == "OPTIONS");
    REQUIRE(got[0].headers.size() == 3);
}

TEST_CASE("two requests in one feed")
{
    Parser p;
    const std::string two = kOptions + "GET_PARAMETER rtsp://1.2.3.4/123 RTSP/1.0\r\nCSeq: 2\r\n\r\n";
    const auto r = p.feed(bytesOf(two));
    REQUIRE(r.ok());
    REQUIRE(r.value().size() == 2);
    REQUIRE(r.value()[0].method == "OPTIONS");
    REQUIRE(r.value()[1].method == "GET_PARAMETER");
    REQUIRE(r.value()[1].uri == "rtsp://1.2.3.4/123");
    REQUIRE(r.value()[1].headers.get("CSeq") == "2");
}

TEST_CASE("request with body uses Content-Length and leaves the rest buffered")
{
    Parser p;
    const std::string msg = "SET_PARAMETER rtsp://x/1 RTSP/1.0\r\n"
                            "Content-Type: text/parameters\r\n"
                            "Content-Length: 12\r\n"
                            "CSeq: 9\r\n"
                            "\r\n"
                            "volume: -5.0OPTIONS * RTSP/1.0\r\nCSeq: 10\r\n\r\n";
    const auto r = p.feed(bytesOf(msg));
    REQUIRE(r.ok());
    REQUIRE(r.value().size() == 2);
    const Request& req = r.value()[0];
    REQUIRE(req.method == "SET_PARAMETER");
    REQUIRE(std::string(req.body.begin(), req.body.end()) == "volume: -5.0");
    REQUIRE(r.value()[1].method == "OPTIONS");
}

TEST_CASE("body split across feeds")
{
    Parser p;
    const std::string head = "ANNOUNCE rtsp://x/1 RTSP/1.0\r\nContent-Length: 6\r\n\r\nabc";
    auto r1 = p.feed(bytesOf(head));
    REQUIRE(r1.ok());
    REQUIRE(r1.value().empty());
    auto r2 = p.feed(bytesOf("def"));
    REQUIRE(r2.ok());
    REQUIRE(r2.value().size() == 1);
    REQUIRE(std::string(r2.value()[0].body.begin(), r2.value()[0].body.end()) == "abcdef");
}

TEST_CASE("HTTP/1.1 version is accepted and header value spaces are trimmed")
{
    Parser p;
    const auto r = p.feed(bytesOf("POST /feedback HTTP/1.1\r\nCSeq:   4  \r\n\r\n"));
    REQUIRE(r.ok());
    REQUIRE(r.value().size() == 1);
    REQUIRE(r.value()[0].version == "HTTP/1.1");
    REQUIRE(r.value()[0].headers.get("CSeq") == "4");
}

TEST_CASE("rejects request line with two tokens")
{
    Parser p;
    const auto r = p.feed(bytesOf("OPTIONS *\r\nCSeq: 1\r\n\r\n"));
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error().code == ErrorCode::malformedInput);
}

TEST_CASE("rejects unknown protocol version")
{
    Parser p;
    const auto r = p.feed(bytesOf("OPTIONS * RTSP/2.0\r\nCSeq: 1\r\n\r\n"));
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error().code == ErrorCode::malformedInput);
}

TEST_CASE("rejects header line without colon")
{
    Parser p;
    const auto r = p.feed(bytesOf("OPTIONS * RTSP/1.0\r\nCSeq 1\r\n\r\n"));
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error().code == ErrorCode::malformedInput);
}

TEST_CASE("rejects header block over 16 KiB")
{
    Parser p;
    std::string big = "OPTIONS * RTSP/1.0\r\n";
    while (big.size() < kMaxHeaderBlockBytes + 100) {
        big += "X-Filler: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\r\n";
    }
    const auto r = p.feed(bytesOf(big)); // no terminating blank line yet
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error().code == ErrorCode::tooLarge);
}

TEST_CASE("rejects more than 64 headers")
{
    Parser p;
    std::string msg = "OPTIONS * RTSP/1.0\r\n";
    for (size_t i = 0; i < kMaxHeaders + 1; ++i) {
        msg += "X-H" + std::to_string(i) + ": v\r\n";
    }
    msg += "\r\n";
    const auto r = p.feed(bytesOf(msg));
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error().code == ErrorCode::tooLarge);
}

TEST_CASE("rejects Content-Length over 1 MiB")
{
    REQUIRE(kMaxBodyBytes == 1024u * 1024u);
    Parser p;
    const auto r = p.feed(bytesOf("SET_PARAMETER * RTSP/1.0\r\nContent-Length: 1048577\r\n\r\n"));
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error().code == ErrorCode::tooLarge);
}

TEST_CASE("rejects non-numeric or negative Content-Length")
{
    Parser p;
    const auto r = p.feed(bytesOf("SET_PARAMETER * RTSP/1.0\r\nContent-Length: 12abc\r\n\r\n"));
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error().code == ErrorCode::malformedInput);

    Parser q;
    const auto r2 = q.feed(bytesOf("SET_PARAMETER * RTSP/1.0\r\nContent-Length: -1\r\n\r\n"));
    REQUIRE_FALSE(r2.ok());
}

TEST_CASE("rejects garbage that is not an RTSP request line")
{
    Parser p;
    const std::vector<uint8_t> junk{0x80, 0x60, 0x00, 0x01, 0xff, 0xfe, '\r', '\n', '\r', '\n'};
    const auto r = p.feed(junk);
    REQUIRE_FALSE(r.ok());
}
