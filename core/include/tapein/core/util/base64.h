// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tapein::core {

/// Standard base64 alphabet. withPadding=false omits trailing '=' (RAOP's
/// Apple-Challenge/Apple-Response use that form).
std::string base64Encode(std::span<const uint8_t> input, bool withPadding);

/// Decodes standard base64. Missing trailing '=' is accepted; any other
/// irregularity (bad character, whitespace, misplaced or excess padding,
/// impossible length) yields nullopt.
std::optional<std::vector<uint8_t>> base64Decode(std::string_view input);

} // namespace tapein::core
