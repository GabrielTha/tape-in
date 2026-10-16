// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors
//
// Registers a _raop._tcp service with the system mDNSResponder and finds it
// again with DNSServiceBrowse/Resolve. Touches the real network stack, so it
// only runs when TAPEIN_RUN_MDNS_TESTS=1 (on macOS 15+ it also needs the
// Local Network permission for the test binary).

#if defined(__APPLE__)

#include "tapein/core/net/reactor.h"
#include "tapein/platform/dns_sd_advertiser.h"

#include <dns_sd.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <map>
#include <string>

using namespace tapein::core;
using namespace std::chrono_literals;

namespace {

struct Found {
    bool resolved = false;
    uint16_t port = 0;
    std::map<std::string, std::string> txt;
};

void DNSSD_API onResolve(DNSServiceRef, DNSServiceFlags, uint32_t, DNSServiceErrorType err, const char*, const char*,
                         uint16_t port, uint16_t txtLen, const unsigned char* txtRecord, void* context)
{
    auto* found = static_cast<Found*>(context);
    if (err != kDNSServiceErr_NoError) {
        return;
    }
    found->port = ntohs(port);
    const uint16_t count = TXTRecordGetCount(txtLen, txtRecord);
    for (uint16_t i = 0; i < count; ++i) {
        char key[256];
        uint8_t valueLen = 0;
        const void* value = nullptr;
        if (TXTRecordGetItemAtIndex(txtLen, txtRecord, i, sizeof key, key, &valueLen, &value) == kDNSServiceErr_NoError) {
            found->txt[key] = std::string(static_cast<const char*>(value), valueLen);
        }
    }
    found->resolved = true;
}

} // namespace

TEST_CASE("dns_sd publish then browse finds the instance with the expected TXT", "[mdns]")
{
    const char* enabled = std::getenv("TAPEIN_RUN_MDNS_TESTS");
    if (enabled == nullptr || std::string(enabled) != "1") {
        SKIP("set TAPEIN_RUN_MDNS_TESTS=1 to run the mDNS test");
    }

    net::Reactor reactor;
    tapein::platform::DnsSdAdvertiser advertiser;
    platform::ServiceRecord rec;
    rec.instanceName = "0A0B0C0D0E0F@Tape In mDNS Test";
    rec.type = "_raop._tcp";
    rec.port = 51234;
    rec.txt = {{"txtvers", "1"}, {"et", "0,1"}, {"am", "TapeIn,1"}};
    const auto published = advertiser.publish(rec);
    REQUIRE(published.ok());
    REQUIRE(advertiser.pollHandle().has_value());
    reactor.add(*advertiser.pollHandle(), [&] { advertiser.processEvents(); });

    // Resolve directly by name (browse + resolve collapses to one call when the name is known).
    Found found;
    DNSServiceRef resolveRef = nullptr;
    REQUIRE(DNSServiceResolve(&resolveRef, 0, kDNSServiceInterfaceIndexAny, rec.instanceName.c_str(), "_raop._tcp",
                              "local.", onResolve, &found)
            == kDNSServiceErr_NoError);
    reactor.add(DNSServiceRefSockFD(resolveRef), [&] { DNSServiceProcessResult(resolveRef); });

    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!found.resolved && std::chrono::steady_clock::now() < deadline) {
        reactor.runOnce(50ms);
    }
    reactor.remove(DNSServiceRefSockFD(resolveRef));
    DNSServiceRefDeallocate(resolveRef);

    REQUIRE(found.resolved);
    REQUIRE(found.port == 51234);
    REQUIRE(found.txt["txtvers"] == "1");
    REQUIRE(found.txt["et"] == "0,1");
    REQUIRE(found.txt["am"] == "TapeIn,1");

    reactor.remove(*advertiser.pollHandle());
    advertiser.unpublish();
    REQUIRE_FALSE(advertiser.pollHandle().has_value());
}

#endif
