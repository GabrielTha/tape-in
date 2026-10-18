// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/util/format.h"

#include <catch2/catch_test_macros.hpp>

using tapein::core::util::formatClock;

TEST_CASE("formatClock renders m:ss")
{
    CHECK(formatClock(0) == "0:00");
    CHECK(formatClock(61) == "1:01");
    CHECK(formatClock(600) == "10:00");
    CHECK(formatClock(9) == "0:09");
}
