// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace tapein::core::rtp {

/// Largest datagram the receiver accepts; anything bigger is dropped.
inline constexpr size_t kMaxDatagram = 2048;

/// RAOP packet types (second byte with the marker bit masked off).
enum class PayloadType : uint8_t {
    timingRequest = 0x52,
    timingReply = 0x53,
    sync = 0x54,
    resendRequest = 0x55,
    resendReply = 0x56,
    audio = 0x60,
};

struct AudioPacket {
    uint16_t seq;
    uint32_t rtpTimestamp;
    uint32_t ssrc;
    std::span<const uint8_t> payload; ///< encrypted/encoded frames (may be empty)
    bool isResend;
};

struct SyncPacket {
    bool extension; ///< bit 4 of byte 0; set on the first sync after RECORD/FLUSH
    uint32_t rtpTimestampLessLatency;
    uint64_t ntpTime; ///< NTP 32.32 on the sender's clock
    uint32_t rtpTimestampNow;
    [[nodiscard]] uint32_t latencyFrames() const noexcept { return rtpTimestampNow - rtpTimestampLessLatency; }
};

struct TimingReply {
    uint64_t originTime; ///< our transmit time echoed back
    uint64_t receiveTime; ///< sender's receive time
    uint64_t transmitTime; ///< sender's transmit time
};

/// Type of a datagram, or nullopt if shorter than 2 bytes or unknown.
std::optional<PayloadType> payloadType(std::span<const uint8_t> datagram) noexcept;

/// Parses a 0x60 audio packet (>= 12 bytes) or a 0x56 resend reply
/// (>= 16 bytes: 4-byte wrapper then the original packet).
std::optional<AudioPacket> parseAudio(std::span<const uint8_t> datagram) noexcept;

/// Parses a 0x54 sync packet (>= 20 bytes).
std::optional<SyncPacket> parseSync(std::span<const uint8_t> datagram) noexcept;

/// Parses a 0x53 timing reply (>= 32 bytes).
std::optional<TimingReply> parseTimingReply(std::span<const uint8_t> datagram) noexcept;

/// 80 D5 <seq> <first missing> <count>, sent to the sender's control port.
std::array<uint8_t, 8> buildResendRequest(uint16_t seqOfRequest, uint16_t firstMissing, uint16_t count) noexcept;

/// 80 D2 00 07 + 20 zero bytes + transmit time (NTP 32.32), sent to the
/// sender's timing port.
std::array<uint8_t, 32> buildTimingRequest(uint64_t transmitNtp) noexcept;

/// Unix nanoseconds <-> NTP 32.32 (seconds since 1900 in the high word).
uint64_t ntpFromNanoseconds(uint64_t unixNs) noexcept;
uint64_t nanosecondsFromNtp(uint64_t ntp) noexcept;

} // namespace tapein::core::rtp
