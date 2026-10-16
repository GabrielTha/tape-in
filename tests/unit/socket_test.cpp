// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/net/socket.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

using namespace tapein::core::net;
using tapein::core::ErrorCode;
using tapein::core::makeError;
using tapein::core::Result;
using namespace std::chrono_literals;

namespace {

Endpoint loopbackV4(uint16_t port)
{
    Endpoint e;
    e.isV6 = false;
    e.address = {127, 0, 0, 1};
    e.port = port;
    return e;
}

template <class F>
bool waitUntil(F&& f, std::chrono::milliseconds timeout = 2000ms)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (f()) {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    }
    return false;
}

} // namespace

TEST_CASE("Endpoint formats v4 and v6 and exposes address bytes")
{
    const Endpoint v4 = loopbackV4(5000);
    REQUIRE(v4.toString() == "127.0.0.1:5000");
    REQUIRE(v4.addressBytes().size() == 4);

    Endpoint v6;
    v6.isV6 = true;
    v6.address = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    v6.port = 7000;
    REQUIRE(v6.toString() == "[::1]:7000");
    REQUIRE(v6.addressBytes().size() == 16);
}

TEST_CASE("TcpListener binds a dynamic port and accepts a connection")
{
    auto listener = TcpListener::bind(loopbackV4(0));
    REQUIRE(listener.ok());
    const auto local = listener.value().localEndpoint();
    REQUIRE(local.ok());
    REQUIRE(local.value().port != 0);

    auto client = TcpSocket::connect(local.value());
    REQUIRE(client.ok());

    Result<TcpSocket> accepted = makeError(ErrorCode::ioError, "none");
    REQUIRE(waitUntil([&] {
        accepted = listener.value().accept();
        return accepted.ok();
    }));

    const std::string hello = "hello";
    auto wrote = client.value().write(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(hello.data()), hello.size()));
    REQUIRE(wrote.ok());
    REQUIRE(wrote.value() == hello.size());

    std::array<uint8_t, 64> buf{};
    size_t got = 0;
    REQUIRE(waitUntil([&] {
        auto r = accepted.value().read(buf);
        REQUIRE(r.ok());
        got += r.value();
        return got == hello.size();
    }));
    REQUIRE(std::string(buf.begin(), buf.begin() + 5) == "hello");

    const auto peer = accepted.value().peerEndpoint();
    REQUIRE(peer.ok());
    REQUIRE(peer.value().addressBytes().size() == 4);
    REQUIRE(accepted.value().localEndpoint().value().port == local.value().port);
}

TEST_CASE("TcpSocket read reports EOF after peer closes")
{
    auto listener = TcpListener::bind(loopbackV4(0));
    REQUIRE(listener.ok());
    auto client = TcpSocket::connect(listener.value().localEndpoint().value());
    REQUIRE(client.ok());
    Result<TcpSocket> accepted = makeError(ErrorCode::ioError, "none");
    REQUIRE(waitUntil([&] {
        accepted = listener.value().accept();
        return accepted.ok();
    }));
    client.value().close();
    std::array<uint8_t, 8> buf{};
    REQUIRE(waitUntil([&] {
        auto r = accepted.value().read(buf);
        return !r.ok() && r.error().code == ErrorCode::ioError; // EOF surfaces as an error
    }));
}

TEST_CASE("UdpSocket round trip on loopback reports local endpoint bytes")
{
    auto a = UdpSocket::bind(loopbackV4(0));
    auto b = UdpSocket::bind(loopbackV4(0));
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    const auto bLocal = b.value().localEndpoint();
    REQUIRE(bLocal.ok());
    REQUIRE(bLocal.value().addressBytes().size() == 4);
    REQUIRE(bLocal.value().addressBytes()[0] == 127);

    const std::array<uint8_t, 4> msg{1, 2, 3, 4};
    auto sent = a.value().sendTo(msg, bLocal.value());
    REQUIRE(sent.ok());
    REQUIRE(sent.value() == 4);

    std::array<uint8_t, 16> buf{};
    Result<std::pair<size_t, Endpoint>> got = makeError(ErrorCode::ioError, "none");
    REQUIRE(waitUntil([&] {
        got = b.value().recvFrom(buf);
        return got.ok() && got.value().first == 4;
    }));
    REQUIRE(buf[3] == 4);
    REQUIRE(got.value().second.port == a.value().localEndpoint().value().port);
}

TEST_CASE("non-blocking read returns zero bytes when nothing is available")
{
    auto s = UdpSocket::bind(loopbackV4(0));
    REQUIRE(s.ok());
    std::array<uint8_t, 8> buf{};
    auto r = s.value().recvFrom(buf);
    REQUIRE(r.ok());
    REQUIRE(r.value().first == 0);
}
