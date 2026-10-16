// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/net/reactor.h"
#include "tapein/core/net/socket.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <thread>

using namespace tapein::core::net;
using namespace std::chrono_literals;

namespace {
Endpoint loopbackV4(uint16_t port)
{
    Endpoint e;
    e.address = {127, 0, 0, 1};
    e.port = port;
    return e;
}
} // namespace

TEST_CASE("Reactor fires readable callback for a UDP socket")
{
    Reactor reactor;
    auto rx = UdpSocket::bind(loopbackV4(0));
    auto tx = UdpSocket::bind(loopbackV4(0));
    REQUIRE(rx.ok());
    REQUIRE(tx.ok());

    int readable = 0;
    std::array<uint8_t, 16> buf{};
    reactor.add(rx.value().handle(), [&] {
        auto r = rx.value().recvFrom(buf);
        if (r.ok() && r.value().first > 0) {
            ++readable;
        }
    });

    const std::array<uint8_t, 3> msg{7, 8, 9};
    REQUIRE(tx.value().sendTo(msg, rx.value().localEndpoint().value()).ok());
    for (int i = 0; i < 50 && readable == 0; ++i) {
        reactor.runOnce(20ms);
    }
    REQUIRE(readable == 1);
    REQUIRE(buf[2] == 9);

    reactor.remove(rx.value().handle());
    REQUIRE(tx.value().sendTo(msg, rx.value().localEndpoint().value()).ok());
    for (int i = 0; i < 5; ++i) {
        reactor.runOnce(10ms);
    }
    REQUIRE(readable == 1); // removed handles no longer fire
}

TEST_CASE("Reactor timers fire once and repeatedly and can be cancelled")
{
    Reactor reactor;
    int once = 0;
    int repeated = 0;
    reactor.addTimer(20ms, [&] { ++once; }, false);
    const auto id = reactor.addTimer(10ms, [&] { ++repeated; }, true);

    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < 120ms) {
        reactor.runOnce(5ms);
    }
    REQUIRE(once == 1);
    REQUIRE(repeated >= 3);

    reactor.cancelTimer(id);
    const int snapshot = repeated;
    for (int i = 0; i < 10; ++i) {
        reactor.runOnce(5ms);
    }
    REQUIRE(repeated == snapshot);
}

TEST_CASE("Reactor post from another thread runs on the reactor and stop ends run")
{
    Reactor reactor;
    std::atomic<bool> ran{false};
    std::thread::id reactorThread;

    std::thread worker([&] {
        std::this_thread::sleep_for(20ms);
        reactor.post([&] {
            ran = true;
            reactorThread = std::this_thread::get_id();
        });
        std::this_thread::sleep_for(20ms);
        reactor.stop();
    });

    reactor.run(); // returns after stop()
    worker.join();
    REQUIRE(ran);
    REQUIRE(reactorThread == std::this_thread::get_id());
}

TEST_CASE("Reactor runOnce with no handles returns after the wait")
{
    Reactor reactor;
    const auto start = std::chrono::steady_clock::now();
    reactor.runOnce(30ms);
    REQUIRE(std::chrono::steady_clock::now() - start >= 25ms);
}
