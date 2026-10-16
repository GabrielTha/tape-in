// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/rtp/rtp_packets.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using namespace tapein::core::rtp;

TEST_CASE("payloadType masks the marker bit")
{
    const std::vector<uint8_t> audio{0x80, 0xE0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3};
    REQUIRE(payloadType(audio) == PayloadType::audio);
    const std::vector<uint8_t> sync{0x80, 0xD4};
    REQUIRE(payloadType(sync) == PayloadType::sync);
    const std::vector<uint8_t> tooShort{0x80};
    REQUIRE_FALSE(payloadType(tooShort).has_value());
    const std::vector<uint8_t> unknown{0x80, 0x99};
    REQUIRE_FALSE(payloadType(unknown).has_value());
}

TEST_CASE("parseAudio reads seq timestamp ssrc and payload")
{
    const std::vector<uint8_t> pkt{0x80, 0x60, 0x12, 0x34, 0x00, 0x01, 0xE2, 0x40, 0xAA, 0xBB, 0xCC, 0xDD, 0xF0, 0xF1, 0xF2};
    const auto a = parseAudio(pkt);
    REQUIRE(a.has_value());
    REQUIRE(a->seq == 0x1234);
    REQUIRE(a->rtpTimestamp == 0x0001E240u);
    REQUIRE(a->ssrc == 0xAABBCCDDu);
    REQUIRE_FALSE(a->isResend);
    REQUIRE(a->payload.size() == 3);
    REQUIRE(a->payload[0] == 0xF0);
}

TEST_CASE("parseAudio resend reply skips 4-byte prefix")
{
    // 0x56 reply: 4-byte wrapper (80 56 seq seq) then the original audio packet.
    const std::vector<uint8_t> pkt{0x80, 0x56, 0x00, 0x01, 0x80, 0x60, 0x00, 0x07, 0, 0, 0, 0x10, 0, 0, 0, 0, 0xAB};
    const auto a = parseAudio(pkt);
    REQUIRE(a.has_value());
    REQUIRE(a->isResend);
    REQUIRE(a->seq == 7);
    REQUIRE(a->rtpTimestamp == 0x10u);
    REQUIRE(a->payload.size() == 1);
    REQUIRE(a->payload[0] == 0xAB);
}

TEST_CASE("parseAudio rejects short packets and foreign types")
{
    std::vector<uint8_t> eleven(11, 0);
    eleven[1] = 0x60;
    REQUIRE_FALSE(parseAudio(eleven).has_value());
    std::vector<uint8_t> fifteenResend(15, 0);
    fifteenResend[1] = 0x56;
    REQUIRE_FALSE(parseAudio(fifteenResend).has_value());
    std::vector<uint8_t> sync(20, 0);
    sync[1] = 0x54;
    REQUIRE_FALSE(parseAudio(sync).has_value());
    std::vector<uint8_t> twelve(12, 0);
    twelve[1] = 0x60;
    const auto a = parseAudio(twelve);
    REQUIRE(a.has_value());
    REQUIRE(a->payload.empty());
}

TEST_CASE("parseSync fields and extension flag")
{
    const std::vector<uint8_t> pkt{0x90, 0xD4, 0x00, 0x07, // marker+extension, type, seq
                                   0x00, 0x00, 0x10, 0x00, // rtp timestamp less latency = 4096
                                   0x83, 0xAA, 0x7E, 0x80, 0x00, 0x00, 0x00, 0x01, // ntp
                                   0x00, 0x01, 0x68, 0x88}; // rtp timestamp now = 92296
    const auto s = parseSync(pkt);
    REQUIRE(s.has_value());
    REQUIRE(s->extension);
    REQUIRE(s->rtpTimestampLessLatency == 4096u);
    REQUIRE(s->ntpTime == 0x83AA7E8000000001ull);
    REQUIRE(s->rtpTimestampNow == 92296u);
    REQUIRE(s->latencyFrames() == 92296u - 4096u);

    std::vector<uint8_t> noExt = pkt;
    noExt[0] = 0x80;
    REQUIRE_FALSE(parseSync(noExt)->extension);
}

TEST_CASE("parseSync rejects 19 bytes and wrong type")
{
    std::vector<uint8_t> nineteen(19, 0);
    nineteen[1] = 0xD4;
    REQUIRE_FALSE(parseSync(nineteen).has_value());
    std::vector<uint8_t> audio(20, 0);
    audio[1] = 0x60;
    REQUIRE_FALSE(parseSync(audio).has_value());
}

TEST_CASE("parseTimingReply reads three ntp timestamps")
{
    std::vector<uint8_t> pkt(32, 0);
    pkt[0] = 0x80;
    pkt[1] = 0xD3;
    pkt[8] = 0x01; // origin
    pkt[16] = 0x02; // receive
    pkt[24] = 0x03; // transmit
    pkt[31] = 0x04;
    const auto t = parseTimingReply(pkt);
    REQUIRE(t.has_value());
    REQUIRE(t->originTime == 0x0100000000000000ull);
    REQUIRE(t->receiveTime == 0x0200000000000000ull);
    REQUIRE(t->transmitTime == 0x0300000000000004ull);
}

TEST_CASE("parseTimingReply rejects 31 bytes")
{
    std::vector<uint8_t> pkt(31, 0);
    pkt[1] = 0xD3;
    REQUIRE_FALSE(parseTimingReply(pkt).has_value());
}

TEST_CASE("buildResendRequest bytes")
{
    const auto r = buildResendRequest(0x0005, 0x1234, 3);
    REQUIRE(r == std::array<uint8_t, 8>{0x80, 0xD5, 0x00, 0x05, 0x12, 0x34, 0x00, 0x03});
}

TEST_CASE("buildTimingRequest layout")
{
    const auto t = buildTimingRequest(0x1122334455667788ull);
    REQUIRE(t.size() == 32);
    REQUIRE(t[0] == 0x80);
    REQUIRE(t[1] == 0xD2);
    REQUIRE(t[2] == 0x00);
    REQUIRE(t[3] == 0x07);
    for (size_t i = 4; i < 24; ++i) {
        REQUIRE(t[i] == 0);
    }
    REQUIRE(t[24] == 0x11);
    REQUIRE(t[31] == 0x88);
}

TEST_CASE("ntp conversion round trip")
{
    const uint64_t unixNs = 1'700'000'000'123'456'789ull;
    const uint64_t ntp = ntpFromNanoseconds(unixNs);
    REQUIRE((ntp >> 32) == 1'700'000'000ull + 2'208'988'800ull);
    const uint64_t back = nanosecondsFromNtp(ntp);
    // fraction has 32 bits ≈ 0.23 ns resolution; allow 1 ns
    REQUIRE(back >= unixNs - 1);
    REQUIRE(back <= unixNs + 1);
}
