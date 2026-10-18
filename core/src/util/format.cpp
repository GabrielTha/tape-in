// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/util/format.h"

namespace tapein::core::util {

std::string formatClock(uint32_t seconds)
{
    const uint32_t minutes = seconds / 60;
    const uint32_t secs = seconds % 60;
    std::string out = std::to_string(minutes);
    out += ':';
    if (secs < 10) {
        out += '0';
    }
    out += std::to_string(secs);
    return out;
}

} // namespace tapein::core::util
