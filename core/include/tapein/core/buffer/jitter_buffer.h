// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace tapein::core::buffer {

inline constexpr size_t kSlots = 512; ///< ≈ 4.1 s at 352 frames per packet
inline constexpr size_t kFramesPerSlot = 352;
inline constexpr size_t kChannels = 2;

struct ReleasedBlock {
    uint16_t seq;
    uint32_t rtpTimestamp;
    bool wasMissing; ///< true → pcm is silence standing in for a lost packet
    std::span<const int16_t> pcm; ///< framesPerPacket * 2 interleaved samples
};

struct MissingRange {
    uint16_t first;
    uint16_t count;
};

/// Sequence-indexed reorder buffer for one session, owned by the network
/// thread. Packets are released strictly in sequence order once they are
/// older than a hold-back window measured in packets behind the newest
/// sequence seen; gaps still open at release time become silence.
class JitterBuffer {
public:
    using Clock = std::chrono::steady_clock;
    using ReleaseFn = std::function<void(const ReleasedBlock&)>;

    explicit JitterBuffer(size_t framesPerPacket = kFramesPerSlot);

    /// Forgets everything (FLUSH to the beginning, new stream).
    void reset() noexcept;

    /// Stores a decoded packet. Returns false and stores nothing if pcm has
    /// the wrong size, the packet is a duplicate, lies behind the release
    /// point, or is kSlots or more ahead of it.
    bool insert(uint16_t seq, uint32_t rtpTimestamp, std::span<const int16_t> pcm, Clock::time_point now);

    /// Releases, in order, every slot whose sequence is at least
    /// holdBackPackets behind the newest sequence seen. Missing slots are
    /// released as silence with wasMissing = true and an interpolated
    /// timestamp. Stops at the first slot that is not yet releasable.
    void release(uint16_t holdBackPackets, Clock::time_point now, const ReleaseFn& fn);

    /// Gaps between the release point and the newest sequence that were
    /// first noticed at least firstRequestAfter ago and were not requested
    /// within the last repeatEvery. Consecutive gaps are coalesced. Marks
    /// the returned gaps as requested now.
    std::vector<MissingRange> dueResendRequests(Clock::time_point now, std::chrono::milliseconds firstRequestAfter,
                                                std::chrono::milliseconds repeatEvery);

    /// Drops every stored packet whose timestamp is before rtpTimestamp and
    /// moves the release point past them (FLUSH).
    void flushBefore(uint32_t rtpTimestamp) noexcept;

    [[nodiscard]] std::optional<uint16_t> newestSeq() const noexcept;
    [[nodiscard]] size_t pendingPackets() const noexcept { return m_filledCount; }
    [[nodiscard]] size_t framesPerPacket() const noexcept { return m_framesPerPacket; }

private:
    struct Slot {
        uint16_t seq = 0;
        uint32_t rtpTimestamp = 0;
        bool filled = false;
        bool requested = false;
        Clock::time_point firstSeen{};
        Clock::time_point lastRequested{};
    };

    [[nodiscard]] Slot& slotFor(uint16_t seq) noexcept { return m_slots[seq % kSlots]; }
    [[nodiscard]] int16_t* pcmFor(uint16_t seq) noexcept { return m_pcm.data() + (seq % kSlots) * m_samplesPerPacket; }
    void clearSlot(Slot& s, Clock::time_point now) noexcept;

    size_t m_framesPerPacket;
    size_t m_samplesPerPacket;
    std::vector<Slot> m_slots;
    std::vector<int16_t> m_pcm;
    std::vector<int16_t> m_silence;
    bool m_hasStream = false;
    uint16_t m_readSeq = 0; ///< next sequence to release
    uint16_t m_newestSeq = 0;
    uint32_t m_lastReleasedTimestamp = 0;
    bool m_hasReleased = false;
    size_t m_filledCount = 0;
};

} // namespace tapein::core::buffer
