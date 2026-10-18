// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/platform/null_resolver.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>

using tapein::core::ErrorCode;
using tapein::core::makeError;
using tapein::core::Result;
using tapein::core::net::Endpoint;
using tapein::core::platform::ResolvedService;
using tapein::platform::NullServiceResolver;

namespace {
Endpoint endpointOn(uint16_t port)
{
    Endpoint e;
    e.address = {127, 0, 0, 1};
    e.port = port;
    return e;
}
} // namespace

TEST_CASE("NullServiceResolver delivers a canned success to the callback")
{
    ResolvedService svc{"iTunes_Ctrl_X", "host.local.", endpointOn(3689)};
    NullServiceResolver resolver{Result<ResolvedService>(svc)};

    std::optional<ResolvedService> got;
    resolver.resolve("iTunes_Ctrl_X", "_dacp._tcp", [&](Result<ResolvedService> res) {
        if (res.ok())
            got = res.value();
    });

    REQUIRE(got.has_value());
    CHECK(got->endpoint.port == 3689);
    CHECK(got->instanceName == "iTunes_Ctrl_X");
}

TEST_CASE("NullServiceResolver delivers a canned error")
{
    NullServiceResolver resolver{Result<ResolvedService>(makeError(ErrorCode::notFound, "no such service"))};

    bool gotError = false;
    resolver.resolve("x", "_dacp._tcp", [&](Result<ResolvedService> res) { gotError = !res.ok(); });

    CHECK(gotError);
}

TEST_CASE("NullServiceResolver in deferred mode fires on processEvents")
{
    ResolvedService svc{"y", "host.local.", endpointOn(5000)};
    NullServiceResolver resolver{Result<ResolvedService>(svc), /*immediate=*/false};

    bool fired = false;
    resolver.resolve("y", "_dacp._tcp", [&](Result<ResolvedService>) { fired = true; });
    CHECK_FALSE(fired);
    resolver.processEvents();
    CHECK(fired);
}
