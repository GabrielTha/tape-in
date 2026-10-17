// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/util/result.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace tapein::core::buffer {

/// Header of a single-producer / single-consumer PCM ring. It holds offsets
/// and counters only, never pointers, so the same layout works over heap
/// memory (the app) and over a memory-mapped region (the v2 plugin).
/// Samples (interleaved int16) follow the header immediately. The region
/// only needs the alignment of a uint32 (mmap and new both exceed it).
struct SpscRingHeader {
    static constexpr uint32_t kMagic = 0x52495054; // "TPIR"
    static constexpr uint32_t kVersion = 1;

    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    uint32_t capacityFrames = 0; ///< power of two
    uint32_t reserved0 = 0;
    std::atomic<uint32_t> writePos{0}; ///< frames written so far (wraps mod 2^32)
    std::atomic<uint32_t> readPos{0}; ///< frames consumed so far
    std::atomic<uint32_t> discardRequest{0}; ///< producer sets 1; consumer clears
    std::atomic<uint32_t> heartbeat{0}; ///< producer increments on every write
    uint32_t reserved[6]{};
};
static_assert(sizeof(SpscRingHeader) == 64);

/// Non-owning view over a ring region. The producer calls write() and
/// requestDiscardAll(); the consumer calls read(), discard() and
/// takeDiscardRequest(). Both may call the size queries. No call allocates
/// or blocks.
class SpscRing {
public:
    SpscRing() = default;

    [[nodiscard]] static size_t bytesRequired(uint32_t capacityFrames, uint32_t channels) noexcept;
    /// Formats region as an empty ring. capacityFrames must be a power of two.
    static Result<SpscRing> create(std::span<std::byte> region, uint32_t capacityFrames, uint32_t sampleRate,
                                   uint32_t channels);
    /// Views a region formatted earlier by create(); validates the header.
    static Result<SpscRing> attach(std::span<std::byte> region);

    // producer
    /// Writes whole frames; interleaved.size() must be a multiple of channels
    /// (otherwise nothing is written). Returns frames written, fewer than
    /// offered when the ring is full.
    uint32_t write(std::span<const int16_t> interleaved) noexcept;
    void requestDiscardAll() noexcept;

    // consumer
    uint32_t read(std::span<int16_t> interleaved) noexcept; ///< frames read
    uint32_t discard(uint32_t frames) noexcept; ///< frames dropped
    /// True once per requestDiscardAll(); drops everything readable.
    bool takeDiscardRequest() noexcept;

    // either side
    [[nodiscard]] uint32_t availableToRead() const noexcept;
    [[nodiscard]] uint32_t availableToWrite() const noexcept;
    [[nodiscard]] uint32_t capacityFrames() const noexcept { return m_header->capacityFrames; }
    [[nodiscard]] uint32_t channels() const noexcept { return m_header->channels; }
    [[nodiscard]] uint32_t sampleRate() const noexcept { return m_header->sampleRate; }
    [[nodiscard]] bool valid() const noexcept { return m_header != nullptr; }

private:
    SpscRing(SpscRingHeader* header, int16_t* samples) noexcept : m_header(header), m_samples(samples) {}

    SpscRingHeader* m_header = nullptr;
    int16_t* m_samples = nullptr;
};

/// Owns the bytes of one ring on the heap.
class HeapRing {
public:
    static Result<HeapRing> create(uint32_t capacityFrames, uint32_t sampleRate = 44100, uint32_t channels = 2);
    HeapRing(HeapRing&&) noexcept = default;
    HeapRing& operator=(HeapRing&&) noexcept = default;
    HeapRing(const HeapRing&) = delete;
    HeapRing& operator=(const HeapRing&) = delete;
    ~HeapRing() = default;

    [[nodiscard]] SpscRing& ring() noexcept { return m_ring; }
    [[nodiscard]] const SpscRing& ring() const noexcept { return m_ring; }

private:
    HeapRing(std::unique_ptr<std::byte[]> bytes, SpscRing ring) noexcept : m_bytes(std::move(bytes)), m_ring(ring) {}

    std::unique_ptr<std::byte[]> m_bytes;
    SpscRing m_ring;
};

} // namespace tapein::core::buffer
