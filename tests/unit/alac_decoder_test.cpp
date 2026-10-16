// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/codec/alac_decoder.h"

#include <ALACEncoder.h> // vendored Apple encoder, test-only, for round trips

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

using namespace tapein::core::codec;

namespace {

constexpr std::array<int32_t, 12> kFmtp{96, 352, 0, 16, 40, 10, 14, 2, 255, 0, 0, 44100};

std::vector<int16_t> makeStereoTone(uint32_t frames)
{
    std::vector<int16_t> pcm(static_cast<size_t>(frames) * 2);
    for (uint32_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / 44100.0;
        pcm[i * 2] = static_cast<int16_t>(12000.0 * std::sin(2.0 * M_PI * 440.0 * t));
        pcm[i * 2 + 1] = static_cast<int16_t>(9000.0 * std::sin(2.0 * M_PI * 660.0 * t) + 300.0 * std::sin(2.0 * M_PI * 3000.0 * t));
    }
    return pcm;
}

// Encodes one 352-frame stereo 16-bit packet with Apple's reference encoder.
std::vector<uint8_t> encodePacket(const std::vector<int16_t>& pcm, uint32_t frames)
{
    AudioFormatDescription in{};
    in.mSampleRate = 44100.0;
    in.mFormatID = kALACFormatLinearPCM;
    in.mFormatFlags = kALACFormatFlagIsSignedInteger | kALACFormatFlagIsPacked;
    in.mBytesPerPacket = 4;
    in.mFramesPerPacket = 1;
    in.mBytesPerFrame = 4;
    in.mChannelsPerFrame = 2;
    in.mBitsPerChannel = 16;

    AudioFormatDescription out{};
    out.mSampleRate = 44100.0;
    out.mFormatID = kALACFormatAppleLossless;
    out.mFormatFlags = 1; // kTestFormatFlag_16BitSourceData
    out.mFramesPerPacket = frames;
    out.mChannelsPerFrame = 2;

    ALACEncoder encoder;
    encoder.SetFrameSize(frames);
    REQUIRE(encoder.InitializeEncoder(out) == 0);

    std::vector<uint8_t> input(reinterpret_cast<const uint8_t*>(pcm.data()),
                               reinterpret_cast<const uint8_t*>(pcm.data()) + pcm.size() * sizeof(int16_t));
    std::vector<uint8_t> output(input.size() + 64);
    int32_t numBytes = static_cast<int32_t>(input.size());
    REQUIRE(encoder.Encode(in, out, input.data(), output.data(), &numBytes) == 0);
    REQUIRE(numBytes > 0);
    output.resize(static_cast<size_t>(numBytes));
    return output;
}

} // namespace

TEST_CASE("fromFmtp maps fields")
{
    const AlacConfig c = AlacConfig::fromFmtp(kFmtp);
    REQUIRE(c.framesPerPacket == 352);
    REQUIRE(c.bitDepth == 16);
    REQUIRE(c.pb == 40);
    REQUIRE(c.mb == 10);
    REQUIRE(c.kb == 14);
    REQUIRE(c.channels == 2);
    REQUIRE(c.maxRun == 255);
    REQUIRE(c.sampleRate == 44100);
    const auto cookie = c.magicCookie();
    REQUIRE(cookie.size() == 24);
    REQUIRE(cookie[0] == 0);
    REQUIRE(cookie[1] == 0);
    REQUIRE(cookie[2] == 0x01);
    REQUIRE(cookie[3] == 0x60); // 352 big-endian
    REQUIRE(cookie[5] == 16);
    REQUIRE(cookie[9] == 2);
    REQUIRE(cookie[20] == 0);
    REQUIRE(cookie[21] == 0);
    REQUIRE(cookie[22] == 0xAC);
    REQUIRE(cookie[23] == 0x44); // 44100 big-endian
}

TEST_CASE("round trip 352-frame stereo tone through ALACEncoder then AlacDecoder is bit-exact")
{
    const uint32_t frames = 352;
    const std::vector<int16_t> pcm = makeStereoTone(frames);
    const std::vector<uint8_t> packet = encodePacket(pcm, frames);
    REQUIRE(packet.size() < pcm.size() * sizeof(int16_t)); // it did compress

    auto decoder = AlacDecoder::create(AlacConfig::fromFmtp(kFmtp));
    REQUIRE(decoder.ok());
    std::vector<int16_t> out(frames * 2, 0x7777);
    const auto decoded = decoder.value().decode(packet, out);
    REQUIRE(decoded.ok());
    REQUIRE(decoded.value() == frames);
    REQUIRE(out == pcm);
}

TEST_CASE("decoder can be reused across packets")
{
    const uint32_t frames = 352;
    auto decoder = AlacDecoder::create(AlacConfig::fromFmtp(kFmtp));
    REQUIRE(decoder.ok());
    for (int k = 0; k < 3; ++k) {
        std::vector<int16_t> pcm = makeStereoTone(frames);
        for (auto& s : pcm) {
            s = static_cast<int16_t>(s / (k + 1));
        }
        const auto packet = encodePacket(pcm, frames);
        std::vector<int16_t> out(frames * 2);
        const auto decoded = decoder.value().decode(packet, out);
        REQUIRE(decoded.ok());
        REQUIRE(out == pcm);
    }
}

TEST_CASE("output buffer too small is rejected")
{
    auto decoder = AlacDecoder::create(AlacConfig::fromFmtp(kFmtp));
    REQUIRE(decoder.ok());
    const auto packet = encodePacket(makeStereoTone(352), 352);
    std::vector<int16_t> tooSmall(352);
    REQUIRE_FALSE(decoder.value().decode(packet, tooSmall).ok());
}

TEST_CASE("truncated packet returns error not crash")
{
    auto decoder = AlacDecoder::create(AlacConfig::fromFmtp(kFmtp));
    REQUIRE(decoder.ok());
    const auto packet = encodePacket(makeStereoTone(352), 352);
    std::vector<int16_t> out(352 * 2);
    for (size_t len : {size_t{0}, size_t{1}, size_t{3}, packet.size() / 2, packet.size() - 1}) {
        const std::span<const uint8_t> truncated(packet.data(), len);
        const auto r = decoder.value().decode(truncated, out);
        // Either an error or a short/zero frame count; never a crash, never out-of-bounds (ASan).
        if (r.ok()) {
            REQUIRE(r.value() <= 352);
        }
    }
}

TEST_CASE("random bytes never crash or write past the output buffer")
{
    auto decoder = AlacDecoder::create(AlacConfig::fromFmtp(kFmtp));
    REQUIRE(decoder.ok());
    std::mt19937 rng(12345);
    for (int seed = 0; seed < 1000; ++seed) {
        const size_t len = 1 + (rng() % 1400);
        std::vector<uint8_t> garbage(len);
        for (auto& b : garbage) {
            b = static_cast<uint8_t>(rng());
        }
        std::vector<int16_t> out(352 * 2 + 8, 0x5A5A);
        const auto r = decoder.value().decode(garbage, std::span<int16_t>(out.data(), 352 * 2));
        if (r.ok()) {
            REQUIRE(r.value() <= 352);
        }
        for (size_t i = 352 * 2; i < out.size(); ++i) {
            REQUIRE(out[i] == 0x5A5A); // guard words untouched
        }
    }
}

TEST_CASE("config with unsupported parameters is rejected")
{
    AlacConfig c = AlacConfig::fromFmtp(kFmtp);
    c.bitDepth = 24;
    REQUIRE_FALSE(AlacDecoder::create(c).ok());
    c = AlacConfig::fromFmtp(kFmtp);
    c.channels = 1;
    REQUIRE_FALSE(AlacDecoder::create(c).ok());
    c = AlacConfig::fromFmtp(kFmtp);
    c.framesPerPacket = 0;
    REQUIRE_FALSE(AlacDecoder::create(c).ok());
}
