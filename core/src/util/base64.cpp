// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/util/base64.h"

#include <array>

namespace tapein::core {

namespace {

constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

constexpr std::array<int8_t, 256> makeDecodeTable()
{
    std::array<int8_t, 256> table{};
    table.fill(-1);
    for (size_t i = 0; i < kAlphabet.size(); ++i) {
        table[static_cast<uint8_t>(kAlphabet[i])] = static_cast<int8_t>(i);
    }
    return table;
}

constexpr std::array<int8_t, 256> kDecode = makeDecodeTable();

} // namespace

std::string base64Encode(std::span<const uint8_t> input, bool withPadding)
{
    std::string out;
    out.reserve(((input.size() + 2) / 3) * 4);

    size_t i = 0;
    for (; i + 3 <= input.size(); i += 3) {
        const uint32_t triple = (static_cast<uint32_t>(input[i]) << 16) | (static_cast<uint32_t>(input[i + 1]) << 8)
                                | input[i + 2];
        out.push_back(kAlphabet[(triple >> 18) & 0x3F]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3F]);
        out.push_back(kAlphabet[(triple >> 6) & 0x3F]);
        out.push_back(kAlphabet[triple & 0x3F]);
    }

    const size_t tail = input.size() - i;
    if (tail == 1) {
        const uint32_t v = static_cast<uint32_t>(input[i]) << 16;
        out.push_back(kAlphabet[(v >> 18) & 0x3F]);
        out.push_back(kAlphabet[(v >> 12) & 0x3F]);
        if (withPadding) {
            out.append("==");
        }
    } else if (tail == 2) {
        const uint32_t v = (static_cast<uint32_t>(input[i]) << 16) | (static_cast<uint32_t>(input[i + 1]) << 8);
        out.push_back(kAlphabet[(v >> 18) & 0x3F]);
        out.push_back(kAlphabet[(v >> 12) & 0x3F]);
        out.push_back(kAlphabet[(v >> 6) & 0x3F]);
        if (withPadding) {
            out.push_back('=');
        }
    }
    return out;
}

std::optional<std::vector<uint8_t>> base64Decode(std::string_view input)
{
    // Strip at most two trailing '=' and remember how many we saw.
    size_t padding = 0;
    while (!input.empty() && input.back() == '=' && padding < 2) {
        input.remove_suffix(1);
        ++padding;
    }
    if (!input.empty() && input.back() == '=') {
        return std::nullopt; // three or more '='
    }

    const size_t rem = input.size() % 4;
    if (rem == 1) {
        return std::nullopt; // one leftover sextet can never encode a byte
    }
    if (padding != 0 && rem != 0 && padding != 4 - rem) {
        return std::nullopt; // padding present but inconsistent with length
    }
    if (padding != 0 && rem == 0) {
        return std::nullopt; // e.g. "AAEC=" : padding where none belongs
    }

    std::vector<uint8_t> out;
    out.reserve((input.size() / 4) * 3 + 2);

    uint32_t acc = 0;
    int bits = 0;
    for (const char c : input) {
        const int8_t v = kDecode[static_cast<uint8_t>(c)];
        if (v < 0) {
            return std::nullopt;
        }
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFF));
        }
    }
    return out;
}

} // namespace tapein::core
