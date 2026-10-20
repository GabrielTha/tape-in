// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/dsp/mixer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace tapein::core::dsp {

namespace {

/// Linear below the knee, then a smooth curve that reaches ±1 asymptotically.
float softClip(float x) noexcept
{
    constexpr float kKnee = 0.9f;
    const float a = std::abs(x);
    if (a <= kKnee) {
        return x;
    }
    const float over = (a - kKnee) / (1.0f - kKnee); // 0.. at the knee
    const float shaped = kKnee + (1.0f - kKnee) * (1.0f - std::exp(-over));
    return x < 0.0f ? -shaped : shaped;
}

} // namespace

Mixer::Mixer(uint32_t maxSources, uint32_t maxBlockFrames)
    : m_slots(std::max<uint32_t>(1, maxSources)), m_maxBlock(std::max<uint32_t>(1, maxBlockFrames))
{
    for (auto& s : m_slots) {
        s.store(nullptr, std::memory_order_relaxed);
    }
    m_left.assign(m_maxBlock, 0.0f);
    m_right.assign(m_maxBlock, 0.0f);
    m_retired.reserve(m_slots.size());
}

Mixer::~Mixer()
{
    for (auto& s : m_slots) {
        delete s.exchange(nullptr, std::memory_order_acq_rel);
    }
}

std::optional<uint32_t> Mixer::add(std::unique_ptr<SourceStream> source)
{
    if (!source) {
        return std::nullopt;
    }
    for (uint32_t i = 0; i < m_slots.size(); ++i) {
        SourceStream* expected = nullptr;
        if (m_slots[i].compare_exchange_strong(expected, source.get(), std::memory_order_acq_rel)) {
            (void)source.release();
            return i;
        }
    }
    return std::nullopt;
}

void Mixer::remove(uint32_t slot)
{
    if (slot >= m_slots.size()) {
        return;
    }
    SourceStream* s = m_slots[slot].exchange(nullptr, std::memory_order_acq_rel);
    if (s != nullptr) {
        m_retired.push_back(Retired{std::unique_ptr<SourceStream>(s), m_meters.callbacks.load(std::memory_order_acquire)});
    }
}

void Mixer::collectGarbage()
{
    const bool active = m_audioActive.load(std::memory_order_acquire);
    const uint64_t now = m_meters.callbacks.load(std::memory_order_acquire);
    std::erase_if(m_retired, [&](const Retired& r) { return !active || now > r.retiredAtCallback; });
}

void Mixer::setAudioActive(bool active) noexcept
{
    m_audioActive.store(active, std::memory_order_release);
}

SourceStream* Mixer::source(uint32_t slot) noexcept
{
    return slot < m_slots.size() ? m_slots[slot].load(std::memory_order_acquire) : nullptr;
}

void Mixer::render(float* const* outputs, uint32_t numOutputs, uint32_t frames, double sampleRate) noexcept
{
    for (uint32_t c = 0; c < numOutputs; ++c) {
        if (outputs[c] != nullptr) {
            std::memset(outputs[c], 0, static_cast<size_t>(frames) * sizeof(float));
        }
    }
    uint32_t done = 0;
    while (done < frames) {
        const uint32_t n = std::min(frames - done, m_maxBlock);
        if (numOutputs > 0) {
            // Advance the output pointers by `done` without allocating.
            float* shifted[2] = {outputs[0] != nullptr ? outputs[0] + done : nullptr,
                                 numOutputs > 1 && outputs[1] != nullptr ? outputs[1] + done : nullptr};
            renderChunk(shifted, std::min<uint32_t>(numOutputs, 2), n, sampleRate);
        } else {
            renderChunk(nullptr, 0, n, sampleRate);
        }
        done += n;
    }
    m_meters.callbacks.fetch_add(1, std::memory_order_acq_rel);
}

void Mixer::playbackEnvelope(float* out, size_t bins) const noexcept
{
    if (out == nullptr || bins == 0) {
        return;
    }
    // Slot head % kEnvelopeBins holds the oldest completed bin (it is the one
    // the audio thread overwrites next).
    const uint64_t head = m_envHead.load(std::memory_order_acquire);
    for (size_t i = 0; i < bins; ++i) {
        const size_t src = (head + i * kEnvelopeBins / bins) % kEnvelopeBins;
        out[i] = m_envBins[src].load(std::memory_order_relaxed);
    }
}

void Mixer::renderChunk(float* const* outputs, uint32_t numOutputs, uint32_t frames, double sampleRate) noexcept
{
    std::fill_n(m_left.data(), frames, 0.0f);
    std::fill_n(m_right.data(), frames, 0.0f);

    bool anySolo = false;
    for (auto& slot : m_slots) {
        SourceStream* s = slot.load(std::memory_order_acquire);
        if (s != nullptr && s->params().solo.load(std::memory_order_relaxed)) {
            anySolo = true;
            break;
        }
    }
    for (auto& slot : m_slots) {
        SourceStream* s = slot.load(std::memory_order_acquire);
        if (s == nullptr) {
            continue;
        }
        const float gate = (!anySolo || s->params().solo.load(std::memory_order_relaxed)) ? 1.0f : 0.0f;
        s->render(m_left.data(), m_right.data(), frames, sampleRate, gate);
    }

    float peakL = 0.0f;
    float peakR = 0.0f;
    // ~8 ms of frames per envelope bin, on the audio clock.
    const auto binFrames = std::max<uint32_t>(1, static_cast<uint32_t>(sampleRate * 0.008));
    for (uint32_t i = 0; i < frames; ++i) {
        m_left[i] = softClip(m_left[i]);
        m_right[i] = softClip(m_right[i]);
        const float al = std::abs(m_left[i]);
        const float ar = std::abs(m_right[i]);
        peakL = std::max(peakL, al);
        peakR = std::max(peakR, ar);
        m_envPeak = std::max(m_envPeak, std::max(al, ar));
        if (++m_envFill >= binFrames) {
            const uint64_t head = m_envHead.load(std::memory_order_relaxed);
            m_envBins[head % kEnvelopeBins].store(m_envPeak, std::memory_order_relaxed);
            m_envHead.store(head + 1, std::memory_order_release);
            m_envPeak = 0.0f;
            m_envFill = 0;
        }
    }
    // Accumulate (atomic max) so consumePeaks() sees the loudest moment
    // between UI reads; the UI resets by exchanging to zero.
    const auto accumulate = [](std::atomic<float>& held, float v) noexcept {
        float cur = held.load(std::memory_order_relaxed);
        while (v > cur && !held.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {
        }
    };
    accumulate(m_meters.peakL, peakL);
    accumulate(m_meters.peakR, peakR);

    if (numOutputs == 0) {
        return;
    }
    if (numOutputs == 1) {
        if (outputs[0] != nullptr) {
            for (uint32_t i = 0; i < frames; ++i) {
                outputs[0][i] = 0.5f * (m_left[i] + m_right[i]);
            }
        }
        return;
    }
    if (outputs[0] != nullptr) {
        std::memcpy(outputs[0], m_left.data(), static_cast<size_t>(frames) * sizeof(float));
    }
    if (outputs[1] != nullptr) {
        std::memcpy(outputs[1], m_right.data(), static_cast<size_t>(frames) * sizeof(float));
    }
}

} // namespace tapein::core::dsp
