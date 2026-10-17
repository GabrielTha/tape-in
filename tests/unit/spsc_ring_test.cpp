// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/buffer/spsc_ring.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

using namespace tapein::core::buffer;

namespace {

std::vector<int16_t> frames(uint32_t first, uint32_t count)
{
    std::vector<int16_t> v;
    for (uint32_t i = 0; i < count; ++i) {
        v.push_back(static_cast<int16_t>(first + i));
        v.push_back(static_cast<int16_t>(-(first + i)));
    }
    return v;
}

} // namespace

TEST_CASE("create rejects non-power-of-two capacity and undersized region")
{
    std::vector<std::byte> region(SpscRing::bytesRequired(16, 2));
    REQUIRE_FALSE(SpscRing::create(region, 12, 44100, 2).ok());
    REQUIRE_FALSE(SpscRing::create(std::span(region).first(region.size() - 1), 16, 44100, 2).ok());
    REQUIRE_FALSE(SpscRing::create(region, 0, 44100, 2).ok());
    REQUIRE(SpscRing::create(region, 16, 44100, 2).ok());
}

TEST_CASE("attach validates magic and version")
{
    std::vector<std::byte> region(SpscRing::bytesRequired(16, 2));
    REQUIRE_FALSE(SpscRing::attach(region).ok()); // zeroed memory
    auto ring = SpscRing::create(region, 16, 44100, 2);
    REQUIRE(ring.ok());
    auto again = SpscRing::attach(region);
    REQUIRE(again.ok());
    CHECK(again.value().capacityFrames() == 16);
    CHECK(again.value().channels() == 2);
    CHECK(again.value().sampleRate() == 44100);
    REQUIRE_FALSE(SpscRing::attach(std::span(region).first(8)).ok());
}

TEST_CASE("write then read round-trips frames in order")
{
    auto heap = HeapRing::create(64);
    REQUIRE(heap.ok());
    SpscRing& ring = heap.value().ring();
    CHECK(ring.availableToRead() == 0);
    CHECK(ring.availableToWrite() == 64);

    const auto in = frames(100, 10);
    CHECK(ring.write(in) == 10);
    CHECK(ring.availableToRead() == 10);
    CHECK(ring.availableToWrite() == 54);

    std::vector<int16_t> out(20);
    CHECK(ring.read(std::span(out).first(8)) == 4);
    for (size_t i = 0; i < 8; ++i) {
        CHECK(out[i] == in[i]);
    }
    CHECK(ring.read(out) == 6);
    for (size_t i = 0; i < 12; ++i) {
        CHECK(out[i] == in[8 + i]);
    }
    CHECK(ring.availableToRead() == 0);
}

TEST_CASE("write is partial when full and read is partial when empty")
{
    auto heap = HeapRing::create(8);
    REQUIRE(heap.ok());
    SpscRing& ring = heap.value().ring();
    CHECK(ring.write(frames(0, 6)) == 6);
    CHECK(ring.write(frames(6, 6)) == 2);
    CHECK(ring.availableToWrite() == 0);
    std::vector<int16_t> out(32);
    CHECK(ring.read(out) == 8);
    CHECK(out[14] == 7);
    CHECK(ring.read(out) == 0);
    CHECK(ring.write(std::span<const int16_t>(out).first(3)) == 0); // odd sample count → nothing
}

TEST_CASE("positions wrap around the end of the buffer")
{
    auto heap = HeapRing::create(8);
    REQUIRE(heap.ok());
    SpscRing& ring = heap.value().ring();
    std::vector<int16_t> out(16);
    uint32_t next = 0;
    for (int round = 0; round < 1000; ++round) {
        const uint32_t n = 1 + static_cast<uint32_t>(round % 7);
        REQUIRE(ring.write(frames(next, n)) == n);
        REQUIRE(ring.read(std::span(out).first(n * 2)) == n);
        for (uint32_t i = 0; i < n; ++i) {
            REQUIRE(out[i * 2] == static_cast<int16_t>(next + i));
        }
        next += n;
    }
}

TEST_CASE("discard drops frames and takeDiscardRequest empties the ring once")
{
    auto heap = HeapRing::create(64);
    REQUIRE(heap.ok());
    SpscRing& ring = heap.value().ring();
    REQUIRE(ring.write(frames(0, 20)) == 20);
    CHECK(ring.discard(5) == 5);
    CHECK(ring.availableToRead() == 15);
    std::vector<int16_t> out(2);
    REQUIRE(ring.read(out) == 1);
    CHECK(out[0] == 5);
    CHECK(ring.discard(100) == 14);
    CHECK(ring.availableToRead() == 0);

    REQUIRE(ring.write(frames(0, 20)) == 20);
    CHECK_FALSE(ring.takeDiscardRequest());
    ring.requestDiscardAll();
    CHECK(ring.takeDiscardRequest());
    CHECK(ring.availableToRead() == 0);
    CHECK_FALSE(ring.takeDiscardRequest());
}

TEST_CASE("two threads: producer writes one million frames of a counter, consumer reads them all in order")
{
    auto heap = HeapRing::create(1024);
    REQUIRE(heap.ok());
    SpscRing& ring = heap.value().ring();
    constexpr uint32_t kTotal = 1'000'000;

    std::thread producer([&ring] {
        uint32_t next = 0;
        std::vector<int16_t> chunk;
        while (next < kTotal) {
            const uint32_t n = std::min<uint32_t>(37, kTotal - next);
            chunk = frames(next, n);
            uint32_t written = 0;
            while (written < n) {
                written += ring.write(std::span<const int16_t>(chunk).subspan(written * 2));
            }
            next += n;
        }
    });

    uint32_t expected = 0;
    std::vector<int16_t> out(200);
    bool inOrder = true;
    while (expected < kTotal) {
        const uint32_t n = ring.read(out);
        for (uint32_t i = 0; i < n; ++i) {
            if (out[i * 2] != static_cast<int16_t>(expected + i) || out[i * 2 + 1] != static_cast<int16_t>(-(expected + i))) {
                inOrder = false;
            }
        }
        expected += n;
    }
    producer.join();
    CHECK(inOrder);
    CHECK(ring.availableToRead() == 0);
}
