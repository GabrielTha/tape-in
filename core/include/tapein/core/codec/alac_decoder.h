// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/util/result.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>

namespace tapein::core::codec {

/// ALAC stream configuration (Apple's ALACSpecificConfig), built from the
/// 12 integers of the SDP a=fmtp line.
struct AlacConfig {
    uint32_t framesPerPacket = 352;
    uint8_t compatibleVersion = 0;
    uint8_t bitDepth = 16;
    uint8_t pb = 40;
    uint8_t mb = 10;
    uint8_t kb = 14;
    uint8_t channels = 2;
    uint16_t maxRun = 255;
    uint32_t maxFrameBytes = 0;
    uint32_t avgBitRate = 0;
    uint32_t sampleRate = 44100;

    /// fmtp layout: [1] frames/packet, [2] compatible version, [3] bit depth,
    /// [4] pb, [5] mb, [6] kb, [7] channels, [8] max run, [9] max frame bytes,
    /// [10] avg bit rate, [11] sample rate. Out-of-range values are clamped
    /// to the field width; create() rejects unsupported combinations.
    static AlacConfig fromFmtp(const std::array<int32_t, 12>& fmtp) noexcept;

    /// 24-byte big-endian ALACSpecificConfig, the "magic cookie".
    [[nodiscard]] std::array<uint8_t, 24> magicCookie() const noexcept;
};

/// Bounds-checked wrapper over Apple's reference ALAC decoder. One instance
/// per session; decode() never allocates after create().
class AlacDecoder {
public:
    ~AlacDecoder();
    AlacDecoder(AlacDecoder&&) noexcept;
    AlacDecoder& operator=(AlacDecoder&&) noexcept;
    AlacDecoder(const AlacDecoder&) = delete;
    AlacDecoder& operator=(const AlacDecoder&) = delete;

    /// Only 16-bit stereo with 1..4096 frames per packet is supported in v1.
    static Result<AlacDecoder> create(const AlacConfig& config);

    /// Decodes one packet into interleaved 16-bit samples. `out` must hold at
    /// least framesPerPacket * channels samples. Returns the number of frames
    /// decoded (may be fewer than framesPerPacket for a partial final frame).
    Result<uint32_t> decode(std::span<const uint8_t> packet, std::span<int16_t> out) noexcept;

    [[nodiscard]] const AlacConfig& config() const noexcept { return m_config; }

private:
    explicit AlacDecoder(const AlacConfig& config);
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    AlacConfig m_config;
};

} // namespace tapein::core::codec
