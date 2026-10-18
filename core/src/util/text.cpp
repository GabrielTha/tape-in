// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/util/text.h"

#include <algorithm>

namespace tapein::core::util {

std::string clampUtf8(std::string_view text, size_t maxBytes)
{
    size_t end = std::min(text.size(), maxBytes);
    // If the cut lands in the middle of a multi-byte code point, back up to
    // its start (a continuation byte is 0b10xxxxxx).
    while (end > 0 && end < text.size() && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
        --end;
    }
    std::string out(text.substr(0, end));
    const bool blank = out.find_first_not_of(" \t\r\n") == std::string::npos;
    return blank ? std::string() : out;
}

} // namespace tapein::core::util
