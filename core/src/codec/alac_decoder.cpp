// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/codec/alac_decoder.h"

#include <ALACBitUtilities.h>
#include <ALACDecoder.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

namespace tapein::core::codec {

namespace {

constexpr uint32_t kMaxFramesPerPacket = 4096;
// Apple's bit reader can look a few bytes past the end of a truncated or
// malformed packet; decoding happens from a zero-padded private copy.
constexpr size_t kGuardBytes = 64;
constexpr size_t kMaxPacketBytes = 2048 + 32;

template <class T>
T clampTo(int32_t v) noexcept
{
    if (v < 0) {
        return 0;
    }
    constexpr auto max = static_cast<int64_t>(std::numeric_limits<T>::max());
    return static_cast<T>(std::min<int64_t>(v, max));
}

void putBe32(uint8_t* p, uint32_t v) noexcept
{
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

} // namespace

AlacConfig AlacConfig::fromFmtp(const std::array<int32_t, 12>& fmtp) noexcept
{
    AlacConfig c;
    c.framesPerPacket = clampTo<uint32_t>(fmtp[1]);
    c.compatibleVersion = clampTo<uint8_t>(fmtp[2]);
    c.bitDepth = clampTo<uint8_t>(fmtp[3]);
    c.pb = clampTo<uint8_t>(fmtp[4]);
    c.mb = clampTo<uint8_t>(fmtp[5]);
    c.kb = clampTo<uint8_t>(fmtp[6]);
    c.channels = clampTo<uint8_t>(fmtp[7]);
    c.maxRun = clampTo<uint16_t>(fmtp[8]);
    c.maxFrameBytes = clampTo<uint32_t>(fmtp[9]);
    c.avgBitRate = clampTo<uint32_t>(fmtp[10]);
    c.sampleRate = clampTo<uint32_t>(fmtp[11]);
    return c;
}

std::array<uint8_t, 24> AlacConfig::magicCookie() const noexcept
{
    std::array<uint8_t, 24> cookie{};
    putBe32(&cookie[0], framesPerPacket);
    cookie[4] = compatibleVersion;
    cookie[5] = bitDepth;
    cookie[6] = pb;
    cookie[7] = mb;
    cookie[8] = kb;
    cookie[9] = channels;
    cookie[10] = static_cast<uint8_t>(maxRun >> 8);
    cookie[11] = static_cast<uint8_t>(maxRun);
    putBe32(&cookie[12], maxFrameBytes);
    putBe32(&cookie[16], avgBitRate);
    putBe32(&cookie[20], sampleRate);
    return cookie;
}

struct AlacDecoder::Impl {
    ALACDecoder decoder;
    std::vector<uint8_t> scratch; // packet copy + guard bytes, allocated once
};

AlacDecoder::AlacDecoder(const AlacConfig& config) : m_impl(std::make_unique<Impl>()), m_config(config) {}
AlacDecoder::~AlacDecoder() = default;
AlacDecoder::AlacDecoder(AlacDecoder&&) noexcept = default;
AlacDecoder& AlacDecoder::operator=(AlacDecoder&&) noexcept = default;

Result<AlacDecoder> AlacDecoder::create(const AlacConfig& config)
{
    if (config.bitDepth != 16) {
        return makeError(ErrorCode::unsupported, "only 16-bit ALAC is supported");
    }
    if (config.channels != 2) {
        return makeError(ErrorCode::unsupported, "only stereo ALAC is supported");
    }
    if (config.framesPerPacket < 1 || config.framesPerPacket > kMaxFramesPerPacket) {
        return makeError(ErrorCode::unsupported, "frames per packet out of range");
    }
    if (config.kb > 31) {
        // ag_dec.c computes (1u << kb) - 1 from the cookie.
        return makeError(ErrorCode::unsupported, "ALAC kb parameter out of range");
    }
    AlacDecoder d(config);
    auto cookie = config.magicCookie();
    const int32_t status = d.m_impl->decoder.Init(cookie.data(), static_cast<uint32_t>(cookie.size()));
    if (status != 0) {
        return makeError(ErrorCode::unsupported, "ALAC decoder init failed");
    }
    // Room for the packet, for the escape (uncompressed) path reading a
    // full frame of samples past a truncated packet, and for the bit
    // reader's look-ahead. Allocated once; zeroed past the packet on every
    // decode so reads beyond the datagram see silence, never stale data.
    const size_t escapeBytes = static_cast<size_t>(config.framesPerPacket) * config.channels * 4;
    d.m_impl->scratch.resize(kMaxPacketBytes + escapeBytes + kGuardBytes);
    return d;
}

Result<uint32_t> AlacDecoder::decode(std::span<const uint8_t> packet, std::span<int16_t> out) noexcept
{
    const size_t needed = static_cast<size_t>(m_config.framesPerPacket) * m_config.channels;
    if (out.size() < needed) {
        return makeError(ErrorCode::invalidArgument, "output buffer too small");
    }
    if (packet.empty()) {
        return makeError(ErrorCode::malformedInput, "empty ALAC packet");
    }
    if (packet.size() > kMaxPacketBytes) {
        return makeError(ErrorCode::tooLarge, "ALAC packet too large");
    }

    std::vector<uint8_t>& scratch = m_impl->scratch;
    std::memcpy(scratch.data(), packet.data(), packet.size());
    std::memset(scratch.data() + packet.size(), 0, scratch.size() - packet.size());

    BitBuffer bits{};
    BitBufferInit(&bits, scratch.data(), static_cast<uint32_t>(packet.size() + kGuardBytes));

    uint32_t frames = 0;
    const int32_t status = m_impl->decoder.Decode(&bits, reinterpret_cast<uint8_t*>(out.data()), m_config.framesPerPacket,
                                                  m_config.channels, &frames);
    if (status != 0) {
        return makeError(ErrorCode::malformedInput, "ALAC decode failed");
    }
    if (frames > m_config.framesPerPacket) {
        return makeError(ErrorCode::malformedInput, "ALAC decode returned too many frames");
    }
    return frames;
}

} // namespace tapein::core::codec
