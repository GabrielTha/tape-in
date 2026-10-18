// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/net/reactor.h"
#include "tapein/core/net/socket.h"
#include "tapein/core/session/dacp_client.h"
#include "tapein/platform/null_resolver.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <optional>
#include <string>

using namespace tapein::core;
using namespace std::chrono_literals;
using tapein::core::session::buildDacpGet;
using tapein::core::session::DacpClient;
using tapein::core::session::dacpPath;
using tapein::core::session::dacpVolumePath;
using tapein::core::session::TransportCommand;
using tapein::core::platform::ResolvedService;
using tapein::platform::NullServiceResolver;

namespace {
net::Endpoint loopbackOn(uint16_t port)
{
    net::Endpoint e;
    e.address = {127, 0, 0, 1};
    e.port = port;
    return e;
}
} // namespace

TEST_CASE("dacpPath maps transport commands to control paths")
{
    CHECK(dacpPath(TransportCommand::playPause) == "/ctrl-int/1/playpause");
    CHECK(dacpPath(TransportCommand::next) == "/ctrl-int/1/nextitem");
    CHECK(dacpPath(TransportCommand::previous) == "/ctrl-int/1/previtem");
}

TEST_CASE("dacpVolumePath formats and clamps the device volume")
{
    CHECK(dacpVolumePath(-12.0f) == "/ctrl-int/1/setproperty?dmcp.device-volume=-12.0000");
    CHECK(dacpVolumePath(0.0f) == "/ctrl-int/1/setproperty?dmcp.device-volume=0.0000");
    CHECK(dacpVolumePath(5.0f) == "/ctrl-int/1/setproperty?dmcp.device-volume=0.0000");    // clamp high
    CHECK(dacpVolumePath(-99.0f) == "/ctrl-int/1/setproperty?dmcp.device-volume=-30.0000"); // clamp low
}

TEST_CASE("buildDacpGet emits a complete HTTP request")
{
    CHECK(buildDacpGet("/ctrl-int/1/playpause", "192.168.1.5", 3689, "cafebabe")
          == "GET /ctrl-int/1/playpause HTTP/1.1\r\n"
             "Host: 192.168.1.5:3689\r\n"
             "Active-Remote: cafebabe\r\n"
             "Connection: close\r\n\r\n");
}

TEST_CASE("DacpClient sends the built request to the resolved endpoint")
{
    net::Reactor reactor;
    auto listener = net::TcpListener::bind(loopbackOn(0)).value();
    const uint16_t port = listener.localEndpoint().value().port;

    std::string captured;
    std::optional<net::TcpSocket> accepted;
    reactor.add(listener.handle(), [&] {
        auto s = listener.accept();
        if (s.ok()) {
            accepted.emplace(std::move(s.value()));
            reactor.add(accepted->handle(), [&] {
                std::array<uint8_t, 512> buf{};
                auto n = accepted->read(buf);
                if (n.ok() && n.value() > 0) {
                    captured.append(reinterpret_cast<const char*>(buf.data()), n.value());
                }
            });
        }
    });

    ResolvedService svc{"iTunes_Ctrl_ABC", "host.local.", loopbackOn(port)};
    NullServiceResolver resolver{Result<ResolvedService>(svc)};
    DacpClient client(resolver);
    client.send("ABC", "cafebabe", "/ctrl-int/1/playpause");

    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (captured.empty() && std::chrono::steady_clock::now() < deadline) {
        reactor.runOnce(50ms);
    }
    CHECK(captured == buildDacpGet("/ctrl-int/1/playpause", "127.0.0.1", port, "cafebabe"));
}

TEST_CASE("DacpClient drops a command to an unreachable sender without blocking the reactor")
{
    net::Reactor reactor;
    // Bind then close a listener to obtain a port where nothing is listening.
    uint16_t deadPort = 0;
    {
        auto l = net::TcpListener::bind(loopbackOn(0)).value();
        deadPort = l.localEndpoint().value().port;
    }

    ResolvedService svc{"iTunes_Ctrl_X", "host.local.", loopbackOn(deadPort)};
    NullServiceResolver resolver{Result<ResolvedService>(svc)};
    DacpClient client(resolver);
    client.send("X", "token", "/ctrl-int/1/playpause"); // connect refused, dropped

    bool ran = false;
    reactor.post([&] { ran = true; });
    reactor.runOnce(50ms);
    CHECK(ran);
}
