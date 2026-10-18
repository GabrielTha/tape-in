// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace tapein::core::util {

/// Truncates a UTF-8 string to at most maxBytes without splitting a code
/// point; an all-whitespace or empty result is reported as empty.
[[nodiscard]] std::string clampUtf8(std::string_view text, size_t maxBytes);

} // namespace tapein::core::util
