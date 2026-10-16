// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/cli/args.h"

#include <charconv>
#include <optional>

namespace tapein::cli {

using core::ErrorCode;
using core::makeError;

namespace {

std::optional<uint8_t> hexNibble(char c) noexcept
{
    if (c >= '0' && c <= '9') {
        return static_cast<uint8_t>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<uint8_t>(c - 'a' + 10);
    }
    if (c >= 'A' && c <= 'F') {
        return static_cast<uint8_t>(c - 'A' + 10);
    }
    return std::nullopt;
}

std::optional<core::crypto::HardwareId> parseHwid(std::string_view text)
{
    if (text.size() != 12) {
        return std::nullopt;
    }
    core::crypto::HardwareId id{};
    for (size_t i = 0; i < 6; ++i) {
        const auto hi = hexNibble(text[i * 2]);
        const auto lo = hexNibble(text[i * 2 + 1]);
        if (!hi || !lo) {
            return std::nullopt;
        }
        id[i] = static_cast<uint8_t>((*hi << 4) | *lo);
    }
    return id;
}

template <class T>
std::optional<T> parseNumber(std::string_view text, T max)
{
    T value{};
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size() || value > max) {
        return std::nullopt;
    }
    return value;
}

} // namespace

core::crypto::HardwareId hardwareIdForName(std::string_view name) noexcept
{
    uint64_t hash = 0xcbf29ce484222325ull; // FNV-1a 64
    for (const char ch : name) {
        hash ^= static_cast<unsigned char>(ch);
        hash *= 0x100000001b3ull;
    }
    core::crypto::HardwareId id{};
    for (size_t i = 0; i < 6; ++i) {
        id[i] = static_cast<uint8_t>(hash >> (8 * (5 - i)));
    }
    id[0] = static_cast<uint8_t>((id[0] | 0x02) & 0xFE);
    return id;
}

std::string usageText()
{
    return "tapein-cli: receive AirPlay audio and write it to WAV files\n"
           "\n"
           "Usage: tapein-cli [options]\n"
           "  --name <str>          receiver name shown to senders (default: \"Tape In\")\n"
           "  --out-dir <path>      directory for WAV files (default: .)\n"
           "  --port <n>            RTSP TCP port (default: 0 = dynamic)\n"
           "  --hwid <12 hex>       6-byte device ID (default: derived from --name)\n"
           "  --max-sessions <n>    concurrent sessions accepted (default: 100)\n"
           "  --txt-et <str>        advertised 'et' TXT value (default: 0,1) [experiment]\n"
           "  --no-apple-response   do not answer Apple-Challenge [experiment]\n"
           "  --capture <dir>       write rtsp-<id>.log and rtp-<id>.bin per session\n"
           "  --verbose             log every RTSP request and response\n"
           "  --help                this text\n";
}

Result<Args> parseArgs(int argc, const char* const* argv)
{
    Args args;
    bool hwidGiven = false;

    auto needValue = [&](int& i, std::string_view flag) -> Result<std::string_view> {
        if (i + 1 >= argc) {
            return makeError(ErrorCode::invalidArgument, std::string(flag) + " needs a value");
        }
        ++i;
        return std::string_view(argv[i]);
    };

    for (int i = 1; i < argc; ++i) {
        const std::string_view flag = argv[i];
        if (flag == "--help" || flag == "-h") {
            args.showHelp = true;
        } else if (flag == "--verbose" || flag == "-v") {
            args.verbose = true;
        } else if (flag == "--no-apple-response") {
            args.answerAppleChallenge = false;
        } else if (flag == "--name") {
            auto v = needValue(i, flag);
            if (!v.ok()) {
                return v.error();
            }
            args.name = std::string(v.value());
        } else if (flag == "--out-dir") {
            auto v = needValue(i, flag);
            if (!v.ok()) {
                return v.error();
            }
            args.outDir = std::string(v.value());
        } else if (flag == "--capture") {
            auto v = needValue(i, flag);
            if (!v.ok()) {
                return v.error();
            }
            args.captureDir = std::string(v.value());
        } else if (flag == "--txt-et") {
            auto v = needValue(i, flag);
            if (!v.ok()) {
                return v.error();
            }
            args.txtEt = std::string(v.value());
        } else if (flag == "--port") {
            auto v = needValue(i, flag);
            if (!v.ok()) {
                return v.error();
            }
            const auto port = parseNumber<uint32_t>(v.value(), 65535);
            if (!port) {
                return makeError(ErrorCode::invalidArgument, "--port must be 0..65535");
            }
            args.port = static_cast<uint16_t>(*port);
        } else if (flag == "--max-sessions") {
            auto v = needValue(i, flag);
            if (!v.ok()) {
                return v.error();
            }
            const auto n = parseNumber<uint32_t>(v.value(), 10000);
            if (!n || *n == 0) {
                return makeError(ErrorCode::invalidArgument, "--max-sessions must be 1..10000");
            }
            args.maxSessions = *n;
        } else if (flag == "--hwid") {
            auto v = needValue(i, flag);
            if (!v.ok()) {
                return v.error();
            }
            const auto id = parseHwid(v.value());
            if (!id) {
                return makeError(ErrorCode::invalidArgument, "--hwid must be 12 hex digits");
            }
            args.hardwareId = *id;
            hwidGiven = true;
        } else {
            return makeError(ErrorCode::invalidArgument, "unknown option: " + std::string(flag));
        }
    }
    if (!hwidGiven) {
        args.hardwareId = hardwareIdForName(args.name);
    }
    return args;
}

} // namespace tapein::cli
