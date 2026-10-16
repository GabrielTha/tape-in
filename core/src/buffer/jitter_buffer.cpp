// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/buffer/jitter_buffer.h"

#include <algorithm>

namespace tapein::core::buffer {

namespace {

/// Signed distance a - b in 16-bit sequence space.
int32_t seqDiff(uint16_t a, uint16_t b) noexcept
{
    return static_cast<int16_t>(static_cast<uint16_t>(a - b));
}

} // namespace

JitterBuffer::JitterBuffer(size_t framesPerPacket)
    : m_framesPerPacket(framesPerPacket)
    , m_samplesPerPacket(framesPerPacket * kChannels)
    , m_slots(kSlots)
    , m_pcm(kSlots * m_samplesPerPacket, 0)
    , m_silence(m_samplesPerPacket, 0)
{
}

void JitterBuffer::reset() noexcept
{
    for (auto& s : m_slots) {
        s = Slot{};
    }
    m_hasStream = false;
    m_hasReleased = false;
    m_filledCount = 0;
}

void JitterBuffer::clearSlot(Slot& s, Clock::time_point now) noexcept
{
    if (s.filled) {
        --m_filledCount;
    }
    s.filled = false;
    s.requested = false;
    s.firstSeen = now;
}

bool JitterBuffer::insert(uint16_t seq, uint32_t rtpTimestamp, std::span<const int16_t> pcm, Clock::time_point now)
{
    if (pcm.size() != m_samplesPerPacket) {
        return false;
    }
    if (!m_hasStream) {
        m_hasStream = true;
        m_readSeq = seq;
        m_newestSeq = seq;
        clearSlot(slotFor(seq), now);
    } else {
        const int32_t aheadOfRead = seqDiff(seq, m_readSeq);
        if (aheadOfRead < 0 || aheadOfRead >= static_cast<int32_t>(kSlots)) {
            return false;
        }
        const int32_t aheadOfNewest = seqDiff(seq, m_newestSeq);
        if (aheadOfNewest > 0) {
            // Slots between the old newest and this one are gaps noticed now;
            // they may hold stale data from a full wrap ago.
            for (uint16_t s = static_cast<uint16_t>(m_newestSeq + 1); s != seq; ++s) {
                clearSlot(slotFor(s), now);
            }
            clearSlot(slotFor(seq), now);
            m_newestSeq = seq;
        }
    }

    Slot& slot = slotFor(seq);
    if (slot.filled && slot.seq == seq) {
        return false; // duplicate
    }
    slot.seq = seq;
    slot.rtpTimestamp = rtpTimestamp;
    slot.filled = true;
    ++m_filledCount;
    std::copy(pcm.begin(), pcm.end(), pcmFor(seq));
    return true;
}

void JitterBuffer::release(uint16_t holdBackPackets, Clock::time_point now, const ReleaseFn& fn)
{
    (void)now;
    if (!m_hasStream || seqDiff(m_newestSeq, m_readSeq) < 0) {
        return;
    }
    while (seqDiff(m_newestSeq, m_readSeq) >= static_cast<int32_t>(holdBackPackets)) {
        Slot& slot = slotFor(m_readSeq);
        ReleasedBlock block{};
        block.seq = m_readSeq;
        if (slot.filled && slot.seq == m_readSeq) {
            block.rtpTimestamp = slot.rtpTimestamp;
            block.wasMissing = false;
            block.pcm = std::span<const int16_t>(pcmFor(m_readSeq), m_samplesPerPacket);
            slot.filled = false;
            --m_filledCount;
        } else {
            block.rtpTimestamp = m_hasReleased ? m_lastReleasedTimestamp + static_cast<uint32_t>(m_framesPerPacket) : 0;
            block.wasMissing = true;
            block.pcm = m_silence;
        }
        slot.requested = false;
        m_lastReleasedTimestamp = block.rtpTimestamp;
        m_hasReleased = true;
        ++m_readSeq;
        fn(block);
    }
}

std::vector<MissingRange> JitterBuffer::dueResendRequests(Clock::time_point now, std::chrono::milliseconds firstRequestAfter,
                                                          std::chrono::milliseconds repeatEvery)
{
    std::vector<MissingRange> out;
    if (!m_hasStream || seqDiff(m_newestSeq, m_readSeq) < 0) {
        return out;
    }
    bool inRun = false;
    for (uint16_t s = m_readSeq;; ++s) {
        const bool last = (s == m_newestSeq);
        Slot& slot = slotFor(s);
        const bool missing = !(slot.filled && slot.seq == s);
        bool due = false;
        if (missing) {
            const bool oldEnough = now - slot.firstSeen >= firstRequestAfter;
            const bool notRecentlyAsked = !slot.requested || (now - slot.lastRequested >= repeatEvery);
            due = oldEnough && notRecentlyAsked;
        }
        if (due) {
            slot.requested = true;
            slot.lastRequested = now;
            if (inRun && out.back().count < 0xFFFF) {
                ++out.back().count;
            } else {
                out.push_back(MissingRange{s, 1});
                inRun = true;
            }
        } else {
            inRun = false;
        }
        if (last) {
            break;
        }
    }
    return out;
}

void JitterBuffer::flushBefore(uint32_t rtpTimestamp) noexcept
{
    if (!m_hasStream) {
        return;
    }
    std::optional<uint16_t> firstKept;
    for (uint16_t s = m_readSeq;; ++s) {
        Slot& slot = slotFor(s);
        if (slot.filled && slot.seq == s) {
            if (static_cast<int32_t>(slot.rtpTimestamp - rtpTimestamp) < 0) {
                slot.filled = false;
                slot.requested = false;
                --m_filledCount;
            } else if (!firstKept) {
                firstKept = s;
            }
        }
        if (s == m_newestSeq) {
            break;
        }
    }
    m_lastReleasedTimestamp = rtpTimestamp - static_cast<uint32_t>(m_framesPerPacket);
    m_hasReleased = true;
    if (firstKept) {
        m_readSeq = *firstKept;
    } else {
        // Nothing survived (pause / seek past the buffer): forget the
        // sequence space so the next packet restarts the stream cleanly,
        // without resend requests for the gap or silence before it.
        for (auto& s : m_slots) {
            s = Slot{};
        }
        m_hasStream = false;
        m_filledCount = 0;
    }
}

std::optional<uint16_t> JitterBuffer::newestSeq() const noexcept
{
    if (!m_hasStream) {
        return std::nullopt;
    }
    return m_newestSeq;
}

} // namespace tapein::core::buffer
