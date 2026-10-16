// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/crypto/apple_challenge.h"
#include "tapein/core/util/result.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace tapein::cli {

using core::Result;

struct Args {
    std::string name = "Tape In";
    std::string outDir = ".";
    uint16_t port = 0;
    core::crypto::HardwareId hardwareId{};
    std::string txtEt = "0,1";
    bool answerAppleChallenge = true;
    bool verbose = false;
    size_t maxSessions = 100;
    std::string captureDir;
    bool showHelp = false;
};

/// Parses argv. Unknown flags, missing values and malformed values are errors.
Result<Args> parseArgs(int argc, const char* const* argv);

/// Stable 6-byte ID derived from the receiver name (FNV-1a), with the
/// locally-administered bit set and the multicast bit cleared, so it looks
/// like a private MAC address to senders.
core::crypto::HardwareId hardwareIdForName(std::string_view name) noexcept;

std::string usageText();

} // namespace tapein::cli
