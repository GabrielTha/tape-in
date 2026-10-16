// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/util/base64.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

using tapein::core::base64Decode;
using tapein::core::base64Encode;

TEST_CASE("base64 round trip for every tail length")
{
    for (size_t len = 0; len < 10; ++len) {
        std::vector<uint8_t> in(len);
        for (size_t i = 0; i < len; ++i) {
            in[i] = static_cast<uint8_t>(i * 37 + 11);
        }
        const std::string encoded = base64Encode(in, true);
        REQUIRE(encoded.size() % 4 == 0);
        const auto decoded = base64Decode(encoded);
        REQUIRE(decoded.has_value());
        REQUIRE(*decoded == in);
    }
}

TEST_CASE("base64 known vector")
{
    const std::string text = "Man";
    const std::vector<uint8_t> bytes(text.begin(), text.end());
    REQUIRE(base64Encode(bytes, true) == "TWFu");
    const std::vector<uint8_t> two{'M', 'a'};
    REQUIRE(base64Encode(two, true) == "TWE=");
    REQUIRE(base64Encode(two, false) == "TWE");
    const std::vector<uint8_t> one{'M'};
    REQUIRE(base64Encode(one, true) == "TQ==");
    REQUIRE(base64Encode(one, false) == "TQ");
}

TEST_CASE("base64 decode without padding")
{
    const auto a = base64Decode("AAEC");
    REQUIRE(a.has_value());
    REQUIRE(*a == std::vector<uint8_t>{0, 1, 2});

    const auto b = base64Decode("AAE");
    REQUIRE(b.has_value());
    REQUIRE(*b == std::vector<uint8_t>{0, 1});

    const auto c = base64Decode("AA");
    REQUIRE(c.has_value());
    REQUIRE(*c == std::vector<uint8_t>{0});

    const auto padded = base64Decode("AAE=");
    REQUIRE(padded.has_value());
    REQUIRE(*padded == std::vector<uint8_t>{0, 1});
}

TEST_CASE("base64 rejects invalid input")
{
    REQUIRE_FALSE(base64Decode("AA!A").has_value());
    REQUIRE_FALSE(base64Decode("A").has_value()); // a single char can never encode a byte
    REQUIRE_FALSE(base64Decode("AAE==").has_value()); // too much padding
    REQUIRE_FALSE(base64Decode("AA=A").has_value()); // padding in the middle
    REQUIRE_FALSE(base64Decode("AAE=\n").has_value()); // whitespace is not accepted
}

TEST_CASE("base64 decode of empty string is empty")
{
    const auto d = base64Decode("");
    REQUIRE(d.has_value());
    REQUIRE(d->empty());
}
