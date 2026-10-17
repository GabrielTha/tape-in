// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/platform/null_login_item.h"
#if defined(__APPLE__)
#include "tapein/platform/mac_login_item.h"
#endif

#include <catch2/catch_test_macros.hpp>

using tapein::core::platform::ILoginItem;

namespace {
bool isValidStatus(ILoginItem::Status s)
{
    return s == ILoginItem::Status::notRegistered || s == ILoginItem::Status::enabled
           || s == ILoginItem::Status::requiresApproval || s == ILoginItem::Status::notFound
           || s == ILoginItem::Status::unavailable;
}
} // namespace

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
TEST_CASE("mac login item answers status and setEnabled without crashing")
{
    // Whether SMAppService can register a non-bundled test binary is
    // environment-dependent (it fails locally but may succeed on a CI
    // runner), so assert only that the calls are well-formed and graceful,
    // and undo any registration they manage to make.
    tapein::platform::MacLoginItem item;
    CHECK(isValidStatus(item.status()));
    const auto enableResult = item.setEnabled(true);
    (void)enableResult.ok(); // either outcome is acceptable, just no crash/throw
    CHECK(isValidStatus(item.status()));
    (void)item.setEnabled(false).ok(); // clean up if it did register
    CHECK(isValidStatus(item.status()));
}
#endif
