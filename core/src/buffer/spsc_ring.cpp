// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/buffer/spsc_ring.h"

#include <algorithm>
#include <cstring>
#include <new>

namespace tapein::core::buffer {

namespace {

constexpr uint32_t kMaxCapacityFrames = 1u << 24; // 16 M frames; keeps sizes far from uint32 wrap
constexpr uint32_t kMaxChannels = 8;

bool isPowerOfTwo(uint32_t v) noexcept
{
    return v != 0 && (v & (v - 1)) == 0;
}

} // namespace

size_t SpscRing::bytesRequired(uint32_t capacityFrames, uint32_t channels) noexcept
{
    return sizeof(SpscRingHeader) + static_cast<size_t>(capacityFrames) * channels * sizeof(int16_t);
}

Result<SpscRing> SpscRing::create(std::span<std::byte> region, uint32_t capacityFrames, uint32_t sampleRate,
                                  uint32_t channels)
{
    if (!isPowerOfTwo(capacityFrames) || capacityFrames > kMaxCapacityFrames) {
        return makeError(ErrorCode::invalidArgument, "ring capacity must be a power of two");
    }
    if (channels == 0 || channels > kMaxChannels) {
        return makeError(ErrorCode::invalidArgument, "ring channel count out of range");
    }
    if (region.size() < bytesRequired(capacityFrames, channels)) {
        return makeError(ErrorCode::invalidArgument, "ring region too small");
    }
    if (reinterpret_cast<std::uintptr_t>(region.data()) % alignof(SpscRingHeader) != 0) {
        return makeError(ErrorCode::invalidArgument, "ring region is misaligned");
    }
    auto* header = new (region.data()) SpscRingHeader{};
    header->magic = SpscRingHeader::kMagic;
    header->version = SpscRingHeader::kVersion;
    header->sampleRate = sampleRate;
    header->channels = channels;
    header->capacityFrames = capacityFrames;
    auto* samples = reinterpret_cast<int16_t*>(region.data() + sizeof(SpscRingHeader));
    std::memset(samples, 0, static_cast<size_t>(capacityFrames) * channels * sizeof(int16_t));
    return SpscRing(header, samples);
}

Result<SpscRing> SpscRing::attach(std::span<std::byte> region)
{
    if (region.size() < sizeof(SpscRingHeader)) {
        return makeError(ErrorCode::invalidArgument, "ring region too small for a header");
    }
    if (reinterpret_cast<std::uintptr_t>(region.data()) % alignof(SpscRingHeader) != 0) {
        return makeError(ErrorCode::invalidArgument, "ring region is misaligned");
    }
    auto* header = std::launder(reinterpret_cast<SpscRingHeader*>(region.data()));
    if (header->magic != SpscRingHeader::kMagic || header->version != SpscRingHeader::kVersion) {
        return makeError(ErrorCode::malformedInput, "ring header has the wrong magic or version");
    }
    if (!isPowerOfTwo(header->capacityFrames) || header->capacityFrames > kMaxCapacityFrames || header->channels == 0
        || header->channels > kMaxChannels) {
        return makeError(ErrorCode::malformedInput, "ring header has invalid geometry");
    }
    if (region.size() < bytesRequired(header->capacityFrames, header->channels)) {
        return makeError(ErrorCode::invalidArgument, "ring region smaller than its header claims");
    }
    auto* samples = reinterpret_cast<int16_t*>(region.data() + sizeof(SpscRingHeader));
    return SpscRing(header, samples);
}

uint32_t SpscRing::availableToRead() const noexcept
{
    const uint32_t w = m_header->writePos.load(std::memory_order_acquire);
    const uint32_t r = m_header->readPos.load(std::memory_order_acquire);
    return w - r;
}

uint32_t SpscRing::availableToWrite() const noexcept
{
    return m_header->capacityFrames - availableToRead();
}

uint32_t SpscRing::write(std::span<const int16_t> interleaved) noexcept
{
    const uint32_t ch = m_header->channels;
    if (interleaved.size() % ch != 0) {
        return 0;
    }
    const uint32_t cap = m_header->capacityFrames;
    const uint32_t w = m_header->writePos.load(std::memory_order_relaxed);
    const uint32_t r = m_header->readPos.load(std::memory_order_acquire);
    const uint32_t free = cap - (w - r);
    const uint32_t n = std::min<uint32_t>(free, static_cast<uint32_t>(interleaved.size() / ch));
    if (n == 0) {
        return 0;
    }
    const uint32_t start = w & (cap - 1);
    const uint32_t firstFrames = std::min(n, cap - start);
    std::memcpy(m_samples + static_cast<size_t>(start) * ch, interleaved.data(), static_cast<size_t>(firstFrames) * ch * sizeof(int16_t));
    if (firstFrames < n) {
        std::memcpy(m_samples, interleaved.data() + static_cast<size_t>(firstFrames) * ch,
                    static_cast<size_t>(n - firstFrames) * ch * sizeof(int16_t));
    }
    m_header->writePos.store(w + n, std::memory_order_release);
    m_header->heartbeat.fetch_add(1, std::memory_order_relaxed);
    return n;
}

void SpscRing::requestDiscardAll() noexcept
{
    m_header->discardRequest.store(1, std::memory_order_release);
}

uint32_t SpscRing::read(std::span<int16_t> interleaved) noexcept
{
    const uint32_t ch = m_header->channels;
    const uint32_t cap = m_header->capacityFrames;
    const uint32_t w = m_header->writePos.load(std::memory_order_acquire);
    const uint32_t r = m_header->readPos.load(std::memory_order_relaxed);
    const uint32_t n = std::min<uint32_t>(w - r, static_cast<uint32_t>(interleaved.size() / ch));
    if (n == 0) {
        return 0;
    }
    const uint32_t start = r & (cap - 1);
    const uint32_t firstFrames = std::min(n, cap - start);
    std::memcpy(interleaved.data(), m_samples + static_cast<size_t>(start) * ch, static_cast<size_t>(firstFrames) * ch * sizeof(int16_t));
    if (firstFrames < n) {
        std::memcpy(interleaved.data() + static_cast<size_t>(firstFrames) * ch, m_samples,
                    static_cast<size_t>(n - firstFrames) * ch * sizeof(int16_t));
    }
    m_header->readPos.store(r + n, std::memory_order_release);
    return n;
}

uint32_t SpscRing::discard(uint32_t frames) noexcept
{
    const uint32_t w = m_header->writePos.load(std::memory_order_acquire);
    const uint32_t r = m_header->readPos.load(std::memory_order_relaxed);
    const uint32_t n = std::min(frames, w - r);
    m_header->readPos.store(r + n, std::memory_order_release);
    return n;
}

bool SpscRing::takeDiscardRequest() noexcept
{
    if (m_header->discardRequest.exchange(0, std::memory_order_acq_rel) == 0) {
        return false;
    }
    discard(UINT32_MAX);
    return true;
}

Result<HeapRing> HeapRing::create(uint32_t capacityFrames, uint32_t sampleRate, uint32_t channels)
{
    const size_t bytes = SpscRing::bytesRequired(capacityFrames, channels);
    std::unique_ptr<std::byte[]> mem(new (std::nothrow) std::byte[bytes]);
    if (!mem) {
        return makeError(ErrorCode::tooLarge, "cannot allocate ring");
    }
    auto ring = SpscRing::create(std::span<std::byte>(mem.get(), bytes), capacityFrames, sampleRate, channels);
    if (!ring.ok()) {
        return ring.error();
    }
    return HeapRing(std::move(mem), ring.value());
}

} // namespace tapein::core::buffer
