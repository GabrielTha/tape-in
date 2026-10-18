// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors
//
// Registers a _dacp._tcp instance with the system mDNSResponder and resolves
// it back with DnsSdResolver. Touches the real network stack, so it only runs
// when TAPEIN_RUN_MDNS_TESTS=1 (a Terminal CLI is exempt from the macOS Local
// Network permission, so no grant is needed).

#if defined(__APPLE__)

#include "tapein/core/net/reactor.h"
#include "tapein/platform/dns_sd_advertiser.h"
#include "tapein/platform/dns_sd_resolver.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <optional>
#include <string>

using namespace tapein::core;
using namespace std::chrono_literals;
using tapein::core::platform::ResolvedService;

TEST_CASE("DnsSdResolver resolves an advertised _dacp._tcp instance", "[mdns]")
{
    const char* enabled = std::getenv("TAPEIN_RUN_MDNS_TESTS");
    if (enabled == nullptr || std::string(enabled) != "1") {
        SKIP("set TAPEIN_RUN_MDNS_TESTS=1 to run the mDNS resolver test");
    }

    net::Reactor reactor;
    tapein::platform::DnsSdAdvertiser advertiser;
    platform::ServiceRecord rec;
    rec.instanceName = "iTunes_Ctrl_TEST123DEADBEEF";
    rec.type = "_dacp._tcp";
    rec.port = 50999;
    rec.txt = {{"txtvers", "1"}};
    REQUIRE(advertiser.publish(rec).ok());
    REQUIRE(advertiser.pollHandle().has_value());
    reactor.add(*advertiser.pollHandle(), [&] { advertiser.processEvents(); });

    tapein::platform::DnsSdResolver resolver(reactor);
    std::optional<ResolvedService> got;
    bool failed = false;
    resolver.resolve(rec.instanceName, "_dacp._tcp", [&](Result<ResolvedService> r) {
        if (r.ok())
            got = r.value();
        else
            failed = true;
    });

    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!got && !failed && std::chrono::steady_clock::now() < deadline) {
        reactor.runOnce(50ms);
    }

    REQUIRE(got.has_value());
    CHECK(got->endpoint.port == 50999);
    CHECK(got->endpoint.addressBytes().size() >= 4);

    reactor.remove(*advertiser.pollHandle());
    advertiser.unpublish();
}

#endif
