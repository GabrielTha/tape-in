// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/util/result.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using tapein::core::Error;
using tapein::core::ErrorCode;
using tapein::core::makeError;
using tapein::core::Result;

namespace {

Result<int> parsePositive(int v)
{
    if (v <= 0) {
        return makeError(ErrorCode::invalidArgument, "not positive");
    }
    return v;
}

Result<void> check(bool ok)
{
    if (!ok) {
        return makeError(ErrorCode::invalidState, "bad state");
    }
    return {};
}

} // namespace

TEST_CASE("Result holds value")
{
    const Result<int> r = parsePositive(5);
    REQUIRE(r.ok());
    REQUIRE(static_cast<bool>(r));
    REQUIRE(r.value() == 5);
}

TEST_CASE("Result holds error")
{
    const Result<int> r = parsePositive(-1);
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error().code == ErrorCode::invalidArgument);
    REQUIRE(r.error().message == "not positive");
}

TEST_CASE("Result<void> ok and error")
{
    REQUIRE(check(true).ok());
    const Result<void> bad = check(false);
    REQUIRE_FALSE(bad.ok());
    REQUIRE(bad.error().code == ErrorCode::invalidState);
}

TEST_CASE("Result map transforms value and propagates error")
{
    const Result<std::string> mapped = parsePositive(3).map([](int v) { return std::to_string(v * 2); });
    REQUIRE(mapped.ok());
    REQUIRE(mapped.value() == "6");

    const Result<std::string> failed = parsePositive(0).map([](int v) { return std::to_string(v); });
    REQUIRE_FALSE(failed.ok());
    REQUIRE(failed.error().code == ErrorCode::invalidArgument);
}

TEST_CASE("Result holds move-only value")
{
    Result<std::unique_ptr<int>> r = std::make_unique<int>(7);
    REQUIRE(r.ok());
    std::unique_ptr<int> taken = std::move(r).value();
    REQUIRE(*taken == 7);
}
