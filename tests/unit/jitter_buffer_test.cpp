// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/buffer/jitter_buffer.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <vector>

using namespace tapein::core::buffer;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

namespace {

constexpr size_t kSamples = kFramesPerSlot * 2;

std::vector<int16_t> pcmFor(uint16_t seq)
{
    return std::vector<int16_t>(kSamples, static_cast<int16_t>(seq));
}

struct Collector {
    std::vector<ReleasedBlock> blocks;
    std::vector<int16_t> firstSamples;
    void operator()(const ReleasedBlock& b)
    {
        blocks.push_back(b);
        firstSamples.push_back(b.pcm[0]);
    }
};

const Clock::time_point t0 = Clock::now();

} // namespace

TEST_CASE("in-order packets release only after the hold-back window")
{
    JitterBuffer jb;
    for (uint16_t s = 0; s < 10; ++s) {
        REQUIRE(jb.insert(s, s * 352u, pcmFor(s), t0));
    }
    REQUIRE(jb.newestSeq() == 9);
    REQUIRE(jb.pendingPackets() == 10);

    Collector c;
    jb.release(5, t0, std::ref(c)); // release everything <= 9 - 5 = 4
    REQUIRE(c.blocks.size() == 5);
    for (size_t i = 0; i < 5; ++i) {
        REQUIRE(c.blocks[i].seq == i);
        REQUIRE(c.blocks[i].rtpTimestamp == i * 352u);
        REQUIRE_FALSE(c.blocks[i].wasMissing);
        REQUIRE(c.firstSamples[i] == static_cast<int16_t>(i));
    }
    REQUIRE(jb.pendingPackets() == 5);

    Collector c2;
    jb.release(0, t0, std::ref(c2)); // hold-back 0 releases the rest
    REQUIRE(c2.blocks.size() == 5);
    REQUIRE(c2.blocks[0].seq == 5);
    REQUIRE(c2.blocks[4].seq == 9);
    REQUIRE(jb.pendingPackets() == 0);
}

TEST_CASE("out-of-order packet fills the gap before release")
{
    JitterBuffer jb;
    REQUIRE(jb.insert(0, 0, pcmFor(0), t0));
    REQUIRE(jb.insert(2, 704, pcmFor(2), t0));
    REQUIRE(jb.insert(3, 1056, pcmFor(3), t0));
    REQUIRE(jb.insert(1, 352, pcmFor(1), t0)); // late arrival, still ahead of the release point
    Collector c;
    jb.release(0, t0, std::ref(c));
    REQUIRE(c.blocks.size() == 4);
    for (size_t i = 0; i < 4; ++i) {
        REQUIRE(c.blocks[i].seq == i);
        REQUIRE_FALSE(c.blocks[i].wasMissing);
    }
}

TEST_CASE("missing packet is released as silence with wasMissing")
{
    JitterBuffer jb;
    REQUIRE(jb.insert(0, 0, pcmFor(0), t0));
    REQUIRE(jb.insert(2, 704, pcmFor(2), t0));
    Collector c;
    jb.release(0, t0, std::ref(c));
    REQUIRE(c.blocks.size() == 3);
    REQUIRE(c.blocks[1].seq == 1);
    REQUIRE(c.blocks[1].wasMissing);
    REQUIRE(c.blocks[1].rtpTimestamp == 352u); // interpolated from the neighbour
    for (const int16_t s : c.blocks[1].pcm) {
        REQUIRE(s == 0);
    }
    REQUIRE_FALSE(c.blocks[2].wasMissing);
}

TEST_CASE("late packet after wrap is ignored")
{
    JitterBuffer jb;
    for (uint32_t s = 0xFFF0; s <= 0xFFFF; ++s) {
        REQUIRE(jb.insert(static_cast<uint16_t>(s), 0, pcmFor(static_cast<uint16_t>(s)), t0));
    }
    for (uint16_t s = 0; s <= 5; ++s) {
        REQUIRE(jb.insert(s, 0, pcmFor(s), t0));
    }
    REQUIRE(jb.newestSeq() == 5);
    Collector c;
    jb.release(0, t0, std::ref(c));
    REQUIRE(c.blocks.size() == 22);
    REQUIRE(c.blocks[0].seq == 0xFFF0);
    REQUIRE(c.blocks[15].seq == 0xFFFF);
    REQUIRE(c.blocks[16].seq == 0);
    REQUIRE(c.blocks[21].seq == 5);

    // A packet behind the release point must be ignored and never re-emitted.
    REQUIRE_FALSE(jb.insert(0xFFF2, 0, pcmFor(0xFFF2), t0));
    Collector c2;
    jb.release(0, t0, std::ref(c2));
    REQUIRE(c2.blocks.empty());
    // The next in-order packet still works.
    REQUIRE(jb.insert(6, 0, pcmFor(6), t0));
    jb.release(0, t0, std::ref(c2));
    REQUIRE(c2.blocks.size() == 1);
    REQUIRE(c2.blocks[0].seq == 6);
}

TEST_CASE("duplicate returns false and does not change data")
{
    JitterBuffer jb;
    REQUIRE(jb.insert(7, 100, pcmFor(7), t0));
    REQUIRE_FALSE(jb.insert(7, 100, pcmFor(99), t0));
    REQUIRE(jb.pendingPackets() == 1);
    Collector c;
    jb.release(0, t0, std::ref(c));
    REQUIRE(c.firstSamples[0] == 7);
}

TEST_CASE("packet too far ahead is rejected")
{
    JitterBuffer jb;
    REQUIRE(jb.insert(0, 0, pcmFor(0), t0));
    REQUIRE_FALSE(jb.insert(static_cast<uint16_t>(kSlots + 5), 0, pcmFor(1), t0));
    REQUIRE(jb.insert(static_cast<uint16_t>(kSlots - 1), 0, pcmFor(1), t0));
}

TEST_CASE("dueResendRequests coalesces runs and respects timing")
{
    JitterBuffer jb;
    REQUIRE(jb.insert(0, 0, pcmFor(0), t0));
    REQUIRE(jb.insert(4, 0, pcmFor(4), t0)); // 1,2,3 missing, noticed at t0
    REQUIRE(jb.insert(6, 0, pcmFor(6), t0)); // 5 missing

    // Too early: nothing is due before firstRequestAfter.
    REQUIRE(jb.dueResendRequests(t0 + 50ms, 100ms, 250ms).empty());

    auto due = jb.dueResendRequests(t0 + 120ms, 100ms, 250ms);
    REQUIRE(due.size() == 2);
    REQUIRE(due[0].first == 1);
    REQUIRE(due[0].count == 3);
    REQUIRE(due[1].first == 5);
    REQUIRE(due[1].count == 1);

    // Just requested: not due again until repeatEvery has elapsed.
    REQUIRE(jb.dueResendRequests(t0 + 200ms, 100ms, 250ms).empty());
    due = jb.dueResendRequests(t0 + 400ms, 100ms, 250ms);
    REQUIRE(due.size() == 2);

    // Once a missing packet arrives it is no longer requested.
    REQUIRE(jb.insert(2, 0, pcmFor(2), t0 + 410ms));
    due = jb.dueResendRequests(t0 + 700ms, 100ms, 250ms);
    REQUIRE(due.size() == 3);
    REQUIRE(due[0].first == 1);
    REQUIRE(due[0].count == 1);
    REQUIRE(due[1].first == 3);
    REQUIRE(due[1].count == 1);
    REQUIRE(due[2].first == 5);
}

TEST_CASE("flushBefore drops older timestamps and keeps newer")
{
    JitterBuffer jb;
    for (uint16_t s = 0; s < 6; ++s) {
        REQUIRE(jb.insert(s, s * 352u, pcmFor(s), t0));
    }
    jb.flushBefore(3 * 352u);
    Collector c;
    jb.release(0, t0, std::ref(c));
    REQUIRE(c.blocks.size() == 3);
    REQUIRE(c.blocks[0].seq == 3);
    REQUIRE(c.blocks[2].seq == 5);
}

TEST_CASE("reset forgets everything")
{
    JitterBuffer jb;
    REQUIRE(jb.insert(100, 0, pcmFor(100), t0));
    jb.reset();
    REQUIRE_FALSE(jb.newestSeq().has_value());
    REQUIRE(jb.pendingPackets() == 0);
    REQUIRE(jb.insert(3, 0, pcmFor(3), t0)); // any sequence restarts the stream
    Collector c;
    jb.release(0, t0, std::ref(c));
    REQUIRE(c.blocks.size() == 1);
    REQUIRE(c.blocks[0].seq == 3);
}

TEST_CASE("insert rejects wrong sample count")
{
    JitterBuffer jb;
    std::vector<int16_t> wrong(10, 0);
    REQUIRE_FALSE(jb.insert(0, 0, wrong, t0));
}
