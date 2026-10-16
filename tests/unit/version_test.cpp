// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/version.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("version string is set")
{
    REQUIRE(tapein::core::versionString() == "0.1.0-phase1");
}
