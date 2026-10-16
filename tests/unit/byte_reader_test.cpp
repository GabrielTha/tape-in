// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/util/byte_reader.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

using tapein::core::ByteReader;

TEST_CASE("ByteReader reads big-endian u8 u16 u32 u64")
{
    const std::array<uint8_t, 15> bytes{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    ByteReader r(bytes);
    REQUIRE(r.remaining() == 15);
    REQUIRE(r.u8() == 0x01);
    REQUIRE(r.u16be() == 0x0203);
    REQUIRE(r.u32be() == 0x04050607u);
    REQUIRE(r.u64be() == 0x08090A0B0C0D0E0Full);
    REQUIRE(r.remaining() == 0);
    REQUIRE(r.position() == 15);
}

TEST_CASE("ByteReader refuses reads past end and keeps position")
{
    const std::array<uint8_t, 3> bytes{0xAA, 0xBB, 0xCC};
    ByteReader r(bytes);
    REQUIRE_FALSE(r.u32be().has_value());
    REQUIRE(r.position() == 0);
    REQUIRE(r.u16be() == 0xAABB);
    REQUIRE_FALSE(r.u16be().has_value());
    REQUIRE(r.position() == 2);
    REQUIRE(r.u8() == 0xCC);
    REQUIRE_FALSE(r.u8().has_value());
}

TEST_CASE("ByteReader bytes and skip are bounds-checked")
{
    const std::array<uint8_t, 5> bytes{1, 2, 3, 4, 5};
    ByteReader r(bytes);
    REQUIRE(r.skip(2));
    const auto span = r.bytes(2);
    REQUIRE(span.has_value());
    REQUIRE(span->size() == 2);
    REQUIRE((*span)[0] == 3);
    REQUIRE((*span)[1] == 4);
    REQUIRE_FALSE(r.bytes(2).has_value());
    REQUIRE(r.position() == 4);
    REQUIRE_FALSE(r.skip(2));
    REQUIRE(r.position() == 4);
    REQUIRE(r.skip(1));
    REQUIRE(r.remaining() == 0);
}

TEST_CASE("ByteReader on empty span")
{
    ByteReader r(std::span<const uint8_t>{});
    REQUIRE(r.remaining() == 0);
    REQUIRE_FALSE(r.u8().has_value());
    REQUIRE(r.bytes(0).has_value());
}
