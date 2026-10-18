// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include <cstdint>
#include <string>

namespace tapein::core::util {

/// Formats a number of seconds as "m:ss" (e.g. 61 -> "1:01", 600 -> "10:00").
[[nodiscard]] std::string formatClock(uint32_t seconds);

} // namespace tapein::core::util
