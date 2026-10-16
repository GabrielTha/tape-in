// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/util/result.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace tapein::core::rtsp {

/// Stream parameters negotiated in the ANNOUNCE body (SDP).
struct AnnounceParams {
    /// a=fmtp:96 fields in order; laid out like Apple's ALACSpecificConfig:
    /// [0] payload type, [1] frames/packet, [2] compatible version, [3] bit depth,
    /// [4] pb, [5] mb, [6] kb, [7] channels, [8] max run, [9] max frame bytes,
    /// [10] avg bit rate, [11] sample rate.
    std::array<int32_t, 12> fmtp{96, 352, 0, 16, 40, 10, 14, 2, 255, 0, 0, 44100};
    bool isPcmL16 = false; ///< a=rtpmap:96 L16/44100/2 (pyatv, Home Assistant)
    std::optional<std::array<uint8_t, 16>> aesIv;
    std::optional<std::vector<uint8_t>> rsaAesKey; ///< 256 bytes, RSA-OAEP wrapped
    std::optional<uint32_t> minLatency;
    std::optional<uint32_t> maxLatency;

    [[nodiscard]] int framesPerPacket() const noexcept { return fmtp[1]; }
    [[nodiscard]] int bitDepth() const noexcept { return fmtp[3]; }
    [[nodiscard]] int channels() const noexcept { return fmtp[7]; }
    [[nodiscard]] int sampleRate() const noexcept { return fmtp[11]; }
    [[nodiscard]] bool encrypted() const noexcept { return aesIv.has_value() && rsaAesKey.has_value(); }
};

/// Parses an ANNOUNCE SDP body. Unknown attributes are ignored. Errors
/// (malformedInput) for: aesiv/rsaaeskey of wrong length or only one of
/// them present; frames/packet outside [1, 4096]; sample rate != 44100;
/// channels != 2; bit depth != 16.
Result<AnnounceParams> parseAnnounceSdp(std::span<const uint8_t> body);

} // namespace tapein::core::rtsp
