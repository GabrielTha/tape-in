// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/rtsp/sdp.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

using namespace tapein::core::rtsp;
using tapein::core::ErrorCode;

namespace {

std::span<const uint8_t> bytesOf(std::string_view s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

// 16 bytes 00..0f
const std::string kIvB64 = "AAECAwQFBgcICQoLDA0ODw==";

std::string key256B64()
{
    // 256 bytes of 0x41 ('A') → base64 "QUFB..." (256/3 = 85 groups + 1 byte tail)
    std::string b64;
    for (int i = 0; i < 85; ++i) {
        b64 += "QUFB";
    }
    b64 += "QQ==";
    return b64;
}

} // namespace

TEST_CASE("parses iOS style announce")
{
    const std::string sdp = "v=0\r\n"
                            "o=iTunes 3413821438 0 IN IP4 192.168.1.20\r\n"
                            "s=iTunes\r\n"
                            "c=IN IP4 192.168.1.10\r\n"
                            "t=0 0\r\n"
                            "m=audio 0 RTP/AVP 96\r\n"
                            "a=rtpmap:96 AppleLossless\r\n"
                            "a=fmtp:96 352 0 16 40 10 14 2 255 0 0 44100\r\n"
                            "a=rsaaeskey:"
                            + key256B64()
                            + "\r\n"
                              "a=aesiv:"
                            + kIvB64
                            + "\r\n"
                              "a=min-latency:11025\r\n"
                              "a=max-latency:88200\r\n";
    const auto r = parseAnnounceSdp(bytesOf(sdp));
    REQUIRE(r.ok());
    const AnnounceParams& p = r.value();
    REQUIRE(p.framesPerPacket() == 352);
    REQUIRE(p.sampleRate() == 44100);
    REQUIRE(p.channels() == 2);
    REQUIRE(p.bitDepth() == 16);
    REQUIRE(p.fmtp[8] == 255);
    REQUIRE_FALSE(p.isPcmL16);
    REQUIRE(p.aesIv.has_value());
    REQUIRE((*p.aesIv)[0] == 0x00);
    REQUIRE((*p.aesIv)[15] == 0x0f);
    REQUIRE(p.rsaAesKey.has_value());
    REQUIRE(p.rsaAesKey->size() == 256);
    REQUIRE((*p.rsaAesKey)[0] == 'A');
    REQUIRE(p.minLatency == 11025u);
    REQUIRE(p.maxLatency == 88200u);
    REQUIRE(p.encrypted());
}

TEST_CASE("pyatv L16 announce sets isPcmL16 and no key")
{
    const std::string sdp = "v=0\r\n"
                            "o=iTunes 12345 0 IN IP4 127.0.0.1\r\n"
                            "s=iTunes\r\n"
                            "c=IN IP4 127.0.0.1\r\n"
                            "t=0 0\r\n"
                            "m=audio 0 RTP/AVP 96\r\n"
                            "a=rtpmap:96 L16/44100/2\r\n"
                            "a=fmtp:96 352 0 16 40 10 14 2 255 0 0 44100\r\n";
    const auto r = parseAnnounceSdp(bytesOf(sdp));
    REQUIRE(r.ok());
    REQUIRE(r.value().isPcmL16);
    REQUIRE_FALSE(r.value().encrypted());
    REQUIRE_FALSE(r.value().aesIv.has_value());
    REQUIRE_FALSE(r.value().minLatency.has_value());
}

TEST_CASE("defaults when fmtp is short")
{
    const auto r = parseAnnounceSdp(bytesOf("a=rtpmap:96 AppleLossless\r\na=fmtp:96 352 0\r\n"));
    REQUIRE(r.ok());
    REQUIRE(r.value().fmtp[1] == 352);
    REQUIRE(r.value().fmtp[3] == 16);
    REQUIRE(r.value().fmtp[7] == 2);
    REQUIRE(r.value().fmtp[11] == 44100);
}

TEST_CASE("CR-only and LF-only line endings tolerated")
{
    const auto r = parseAnnounceSdp(bytesOf("a=rtpmap:96 AppleLossless\ra=fmtp:96 352 0 16 40 10 14 2 255 0 0 44100\na=min-latency:4410\n"));
    REQUIRE(r.ok());
    REQUIRE(r.value().minLatency == 4410u);
}

TEST_CASE("rejects aesiv without rsaaeskey")
{
    const auto r = parseAnnounceSdp(bytesOf("a=fmtp:96 352 0 16 40 10 14 2 255 0 0 44100\r\na=aesiv:" + kIvB64 + "\r\n"));
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error().code == ErrorCode::malformedInput);
}

TEST_CASE("rejects aesiv of wrong length")
{
    const auto r = parseAnnounceSdp(
        bytesOf("a=fmtp:96 352 0 16 40 10 14 2 255 0 0 44100\r\na=aesiv:AAEC\r\na=rsaaeskey:" + key256B64() + "\r\n"));
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error().code == ErrorCode::malformedInput);
}

TEST_CASE("rejects rsaaeskey of wrong length")
{
    const auto r = parseAnnounceSdp(bytesOf("a=aesiv:" + kIvB64 + "\r\na=rsaaeskey:QUFB\r\n"));
    REQUIRE_FALSE(r.ok());
}

TEST_CASE("rejects unsupported stream parameters")
{
    REQUIRE_FALSE(parseAnnounceSdp(bytesOf("a=fmtp:96 352 0 16 40 10 14 1 255 0 0 44100\r\n")).ok()); // mono
    REQUIRE_FALSE(parseAnnounceSdp(bytesOf("a=fmtp:96 352 0 24 40 10 14 2 255 0 0 44100\r\n")).ok()); // 24-bit
    REQUIRE_FALSE(parseAnnounceSdp(bytesOf("a=fmtp:96 352 0 16 40 10 14 2 255 0 0 48000\r\n")).ok()); // 48 kHz
    REQUIRE_FALSE(parseAnnounceSdp(bytesOf("a=fmtp:96 0 0 16 40 10 14 2 255 0 0 44100\r\n")).ok());   // 0 frames
    REQUIRE_FALSE(parseAnnounceSdp(bytesOf("a=fmtp:96 5000 0 16 40 10 14 2 255 0 0 44100\r\n")).ok()); // > 4096
}

TEST_CASE("ignores unknown attributes and garbage values")
{
    const auto r = parseAnnounceSdp(bytesOf("a=whatever:zzz\r\na=fmtp:96 352 abc 16 40 10 14 2 255 0 0 44100\r\n"));
    REQUIRE(r.ok());
    REQUIRE(r.value().fmtp[2] == 0); // non-numeric field keeps the default
}
