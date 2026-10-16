// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/rtsp/sdp.h"

#include "tapein/core/util/base64.h"

#include <algorithm>
#include <charconv>
#include <string_view>

namespace tapein::core::rtsp {

namespace {

constexpr size_t kRsaKeyBytes = 256;
constexpr int32_t kMaxFramesPerPacket = 4096;

std::optional<int32_t> parseInt(std::string_view s)
{
    int32_t v = 0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || ptr != s.data() + s.size()) {
        return std::nullopt;
    }
    return v;
}

std::optional<uint32_t> parseUint(std::string_view s)
{
    uint32_t v = 0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || ptr != s.data() + s.size()) {
        return std::nullopt;
    }
    return v;
}

void parseFmtp(std::string_view value, std::array<int32_t, 12>& fmtp)
{
    size_t index = 0;
    size_t pos = 0;
    while (index < fmtp.size() && pos < value.size()) {
        while (pos < value.size() && (value[pos] == ' ' || value[pos] == '\t')) {
            ++pos;
        }
        if (pos >= value.size()) {
            break;
        }
        size_t end = pos;
        while (end < value.size() && value[end] != ' ' && value[end] != '\t') {
            ++end;
        }
        if (const auto v = parseInt(value.substr(pos, end - pos))) {
            fmtp[index] = *v;
        }
        ++index;
        pos = end;
    }
}

Result<void> validate(const AnnounceParams& p)
{
    if (p.aesIv.has_value() != p.rsaAesKey.has_value()) {
        return makeError(ErrorCode::malformedInput, "aesiv and rsaaeskey must both be present or both absent");
    }
    if (p.framesPerPacket() < 1 || p.framesPerPacket() > kMaxFramesPerPacket) {
        return makeError(ErrorCode::malformedInput, "unsupported frames per packet");
    }
    if (p.sampleRate() != 44100) {
        return makeError(ErrorCode::unsupported, "unsupported sample rate");
    }
    if (p.channels() != 2) {
        return makeError(ErrorCode::unsupported, "unsupported channel count");
    }
    if (p.bitDepth() != 16) {
        return makeError(ErrorCode::unsupported, "unsupported bit depth");
    }
    return {};
}

} // namespace

Result<AnnounceParams> parseAnnounceSdp(std::span<const uint8_t> body)
{
    AnnounceParams params;
    const std::string_view text(reinterpret_cast<const char*>(body.data()), body.size());

    size_t pos = 0;
    while (pos < text.size()) {
        const size_t end = text.find_first_of("\r\n", pos);
        const std::string_view line = text.substr(pos, end == std::string_view::npos ? std::string_view::npos : end - pos);
        pos = end == std::string_view::npos ? text.size() : end + 1;

        if (!line.starts_with("a=")) {
            continue;
        }
        const std::string_view attr = line.substr(2);
        const size_t colon = attr.find(':');
        if (colon == std::string_view::npos) {
            continue;
        }
        const std::string_view name = attr.substr(0, colon);
        const std::string_view value = attr.substr(colon + 1);

        if (name == "fmtp") {
            // "96 352 0 16 ..." — first token is the payload type.
            parseFmtp(value, params.fmtp);
        } else if (name == "rtpmap") {
            if (value.find("L16/44100/2") != std::string_view::npos) {
                params.isPcmL16 = true;
            }
        } else if (name == "aesiv") {
            const auto decoded = base64Decode(value);
            if (!decoded || decoded->size() != 16) {
                return makeError(ErrorCode::malformedInput, "aesiv must decode to 16 bytes");
            }
            std::array<uint8_t, 16> iv{};
            std::copy_n(decoded->begin(), 16, iv.begin());
            params.aesIv = iv;
        } else if (name == "rsaaeskey") {
            auto decoded = base64Decode(value);
            if (!decoded || decoded->size() != kRsaKeyBytes) {
                return makeError(ErrorCode::malformedInput, "rsaaeskey must decode to 256 bytes");
            }
            params.rsaAesKey = std::move(*decoded);
        } else if (name == "min-latency") {
            params.minLatency = parseUint(value);
        } else if (name == "max-latency") {
            params.maxLatency = parseUint(value);
        }
    }

    if (const auto v = validate(params); !v.ok()) {
        return v.error();
    }
    return params;
}

} // namespace tapein::core::rtsp
