// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/rtp/rtp_packets.h"

#include "tapein/core/util/byte_reader.h"

namespace tapein::core::rtp {

namespace {

constexpr uint64_t kNtpUnixOffsetSeconds = 2'208'988'800ull; // 1900 → 1970
constexpr uint64_t kNanosPerSecond = 1'000'000'000ull;

void putBe64(uint8_t* p, uint64_t v) noexcept
{
    for (int i = 7; i >= 0; --i) {
        p[i] = static_cast<uint8_t>(v);
        v >>= 8;
    }
}

} // namespace

std::optional<PayloadType> payloadType(std::span<const uint8_t> datagram) noexcept
{
    if (datagram.size() < 2) {
        return std::nullopt;
    }
    switch (datagram[1] & 0x7F) {
    case 0x52:
        return PayloadType::timingRequest;
    case 0x53:
        return PayloadType::timingReply;
    case 0x54:
        return PayloadType::sync;
    case 0x55:
        return PayloadType::resendRequest;
    case 0x56:
        return PayloadType::resendReply;
    case 0x60:
        return PayloadType::audio;
    default:
        return std::nullopt;
    }
}

std::optional<AudioPacket> parseAudio(std::span<const uint8_t> datagram) noexcept
{
    const auto type = payloadType(datagram);
    if (!type) {
        return std::nullopt;
    }
    ByteReader r(datagram);
    bool isResend = false;
    if (*type == PayloadType::resendReply) {
        isResend = true;
        if (!r.skip(4)) {
            return std::nullopt;
        }
        // The wrapped packet must itself be an audio packet.
        if (r.remaining() < 2 || (datagram[5] & 0x7F) != 0x60) {
            return std::nullopt;
        }
    } else if (*type != PayloadType::audio) {
        return std::nullopt;
    }
    if (r.remaining() < 12) {
        return std::nullopt;
    }
    r.skip(2); // version/flags, payload type
    AudioPacket pkt{};
    pkt.seq = *r.u16be();
    pkt.rtpTimestamp = *r.u32be();
    pkt.ssrc = *r.u32be();
    pkt.payload = *r.bytes(r.remaining());
    pkt.isResend = isResend;
    return pkt;
}

std::optional<SyncPacket> parseSync(std::span<const uint8_t> datagram) noexcept
{
    if (payloadType(datagram) != PayloadType::sync || datagram.size() < 20) {
        return std::nullopt;
    }
    ByteReader r(datagram);
    SyncPacket s{};
    s.extension = (datagram[0] & 0x10) != 0;
    r.skip(4);
    s.rtpTimestampLessLatency = *r.u32be();
    s.ntpTime = *r.u64be();
    s.rtpTimestampNow = *r.u32be();
    return s;
}

std::optional<TimingReply> parseTimingReply(std::span<const uint8_t> datagram) noexcept
{
    if (payloadType(datagram) != PayloadType::timingReply || datagram.size() < 32) {
        return std::nullopt;
    }
    ByteReader r(datagram);
    r.skip(8);
    TimingReply t{};
    t.originTime = *r.u64be();
    t.receiveTime = *r.u64be();
    t.transmitTime = *r.u64be();
    return t;
}

std::array<uint8_t, 8> buildResendRequest(uint16_t seqOfRequest, uint16_t firstMissing, uint16_t count) noexcept
{
    return {0x80,
            0xD5,
            static_cast<uint8_t>(seqOfRequest >> 8),
            static_cast<uint8_t>(seqOfRequest),
            static_cast<uint8_t>(firstMissing >> 8),
            static_cast<uint8_t>(firstMissing),
            static_cast<uint8_t>(count >> 8),
            static_cast<uint8_t>(count)};
}

std::array<uint8_t, 32> buildTimingRequest(uint64_t transmitNtp) noexcept
{
    std::array<uint8_t, 32> p{};
    p[0] = 0x80;
    p[1] = 0xD2;
    p[2] = 0x00;
    p[3] = 0x07;
    putBe64(&p[24], transmitNtp);
    return p;
}

uint64_t ntpFromNanoseconds(uint64_t unixNs) noexcept
{
    const uint64_t seconds = unixNs / kNanosPerSecond + kNtpUnixOffsetSeconds;
    const uint64_t nanos = unixNs % kNanosPerSecond;
    // fraction = nanos * 2^32 / 1e9, computed without overflow (nanos < 2^30, 2^32 → product < 2^62)
    const uint64_t fraction = (nanos << 32) / kNanosPerSecond;
    return (seconds << 32) | (fraction & 0xFFFFFFFFull);
}

uint64_t nanosecondsFromNtp(uint64_t ntp) noexcept
{
    const uint64_t seconds = (ntp >> 32) - kNtpUnixOffsetSeconds;
    const uint64_t fraction = ntp & 0xFFFFFFFFull;
    // round to nearest nanosecond
    const uint64_t nanos = (fraction * kNanosPerSecond + (1ull << 31)) >> 32;
    return seconds * kNanosPerSecond + nanos;
}

} // namespace tapein::core::rtp
