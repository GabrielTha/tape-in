// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include <samplerate.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

TEST_CASE("libsamplerate is linked")
{
    REQUIRE(std::string(src_get_version()).find("libsamplerate") != std::string::npos);
}
