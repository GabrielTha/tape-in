// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/platform/null_login_item.h"
#if defined(__APPLE__)
#include "tapein/platform/mac_login_item.h"
#endif

#include <catch2/catch_test_macros.hpp>

using tapein::core::platform::ILoginItem;

TEST_CASE("null login item round-trips enabled and reports notRegistered by default")
{
    tapein::platform::NullLoginItem item;
    CHECK(item.status() == ILoginItem::Status::notRegistered);
    REQUIRE(item.setEnabled(true).ok());
    CHECK(item.status() == ILoginItem::Status::enabled);
    REQUIRE(item.setEnabled(false).ok());
    CHECK(item.status() == ILoginItem::Status::notRegistered);
    item.openSystemSettings(); // no-op, must not crash
}

#if defined(__APPLE__)
TEST_CASE("mac login item outside a bundle reports unavailable and fails setEnabled gracefully")
{
    tapein::platform::MacLoginItem item;
    // Run from the test binary, not a registered .app: no crash, no throw.
    const auto status = item.status();
    CHECK((status == ILoginItem::Status::notFound || status == ILoginItem::Status::unavailable
           || status == ILoginItem::Status::notRegistered));
    const auto r = item.setEnabled(true);
    CHECK_FALSE(r.ok()); // cannot register a non-bundled binary
}
#endif
