// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/dsp/source_stream.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace tapein::core::dsp {

namespace {

constexpr uint32_t kChannels = 2;
constexpr uint32_t kLowWaterFrames = 882; ///< 20 ms at 44.1 kHz: start fading before the ring runs dry
constexpr uint32_t kResyncExcessFrames = 44100; ///< 1 s above target → skip back
constexpr uint32_t kMinTargetFrames = 2048; ///< never accept a target below ~46 ms
constexpr double kFadeSeconds = 0.005;
constexpr uint32_t kInputHeadroom = 64;
constexpr uint32_t kLookahead = 16; ///< frames offered beyond the block's need, for the sinc filter's history
constexpr double kMaxInputPerOutput = 8.0; ///< device rates down to 44100/8 get full blocks
constexpr float kInt16Scale = 1.0f / 32768.0f;

} // namespace

Result<std::unique_ptr<SourceStream>> SourceStream::create(buffer::HeapRing ring, ResamplerQuality quality,
                                                           uint32_t maxBlockFrames)
{
    if (maxBlockFrames == 0 || maxBlockFrames > (1u << 16)) {
        return makeError(ErrorCode::invalidArgument, "maxBlockFrames out of range");
    }
    if (ring.ring().channels() != kChannels || ring.ring().sampleRate() != kRingRate) {
        return makeError(ErrorCode::invalidArgument, "source ring must be stereo at 44.1 kHz");
    }
    auto resampler = Resampler::create(kChannels, quality);
    if (!resampler.ok()) {
        return resampler.error();
    }
    std::unique_ptr<SourceStream> s(new (std::nothrow) SourceStream(std::move(ring), std::move(resampler.value()), maxBlockFrames));
    if (!s) {
        return makeError(ErrorCode::tooLarge, "cannot allocate source stream");
    }
    return s;
}

SourceStream::SourceStream(buffer::HeapRing ring, Resampler resampler, uint32_t maxBlock)
    : m_ring(std::move(ring)), m_resampler(std::move(resampler)), m_maxBlock(maxBlock)
{
    const auto inCapacity = static_cast<size_t>(std::ceil(maxBlock * kMaxInputPerOutput)) + kInputHeadroom;
    m_s16.assign(inCapacity * kChannels, 0);
    m_in.assign(inCapacity * kChannels, 0.0f);
    m_out.assign(static_cast<size_t>(maxBlock) * kChannels, 0.0f);
}

void SourceStream::render(float* left, float* right, uint32_t frames, double deviceSampleRate, float soloGate) noexcept
{
    uint32_t done = 0;
    while (done < frames) {
        const uint32_t n = std::min(frames - done, m_maxBlock);
        renderChunk(left + done, right + done, n, deviceSampleRate, soloGate);
        done += n;
    }
}

void SourceStream::beginFadeOut(StreamState after) noexcept
{
    if (m_state == StreamState::playing) {
        m_state = StreamState::fadingOut;
        m_afterFade = after;
    } else if (m_state == StreamState::fadingOut) {
        // Keep the fade, but a stop wins over anything else.
        if (after == StreamState::stopped) {
            m_afterFade = after;
        }
    } else {
        m_state = after;
        m_fade = 0.0f;
    }
}

void SourceStream::finishFadeOut() noexcept
{
    if (m_pendingDiscard > 0) {
        ring().discard(m_pendingDiscard);
        m_pendingDiscard = 0;
    }
    m_inFrames = 0;
    m_resampler.reset();
    m_drift.reset();
    m_fade = 0.0f;
    m_state = m_afterFade;
}

void SourceStream::beginPlaying() noexcept
{
    m_state = StreamState::playing;
    m_fade = 0.0f;
    m_drift.reset();
}

void SourceStream::handleRequests(uint32_t fill) noexcept
{
    if (ring().takeDiscardRequest()) {
        // FLUSH from the sender: everything readable is gone; start over.
        m_pendingDiscard = 0;
        beginFadeOut(StreamState::buffering);
        if (m_state == StreamState::buffering) {
            m_inFrames = 0;
            m_resampler.reset();
        }
        fill = 0;
    }
    if (m_params.stopRequested.load(std::memory_order_relaxed)) {
        beginFadeOut(StreamState::stopped);
    }
    (void)fill;
}

void SourceStream::renderChunk(float* left, float* right, uint32_t frames, double deviceSampleRate, float soloGate) noexcept
{
    const uint32_t fill = ring().availableToRead();
    m_meters.fillFrames.store(fill + m_inFrames, std::memory_order_relaxed);
    handleRequests(fill);

    const uint32_t target = std::max(kMinTargetFrames, m_params.targetFillFrames.load(std::memory_order_relaxed));

    switch (m_state) {
    case StreamState::stopped:
        break;
    case StreamState::buffering:
        if (fill >= target) {
            beginPlaying();
        }
        break;
    case StreamState::refilling:
        if (fill >= target / 2) {
            beginPlaying();
        }
        break;
    case StreamState::playing:
        if (fill + m_inFrames < kLowWaterFrames) {
            m_meters.underruns.fetch_add(1, std::memory_order_relaxed);
            beginFadeOut(StreamState::refilling);
        } else if (fill > target + kResyncExcessFrames) {
            m_pendingDiscard = fill - target;
            beginFadeOut(StreamState::refilling);
        }
        break;
    case StreamState::fadingOut:
        break;
    }

    if (m_state == StreamState::stopped || m_state == StreamState::buffering || m_state == StreamState::refilling) {
        m_gain = 0.0f;
        m_meters.peakL.store(0.0f, std::memory_order_relaxed);
        m_meters.peakR.store(0.0f, std::memory_order_relaxed);
        m_meters.rms.store(0.0f, std::memory_order_relaxed);
        m_meters.state.store(static_cast<uint8_t>(m_state), std::memory_order_relaxed);
        return;
    }

    // Ratio: device rate over ring rate, steered by the drift loop while playing.
    if (deviceSampleRate <= 0.0) {
        deviceSampleRate = kRingRate;
    }
    const double nominal = deviceSampleRate / kRingRate;
    double multiplier = 1.0 + m_drift.deviation();
    if (m_state == StreamState::playing) {
        multiplier = m_drift.update(static_cast<double>(fill) + m_inFrames, target, frames / deviceSampleRate);
    }
    const double ratio = nominal * multiplier;
    if (m_lastRate != deviceSampleRate) {
        m_resampler.setRatio(ratio); // device switch: no interpolation from the old rate
        m_lastRate = deviceSampleRate;
    }
    m_meters.ratioDeviation.store(static_cast<float>(multiplier - 1.0), std::memory_order_relaxed);

    const uint32_t produced = produce(frames, ratio);
    if (produced < frames) {
        std::fill(m_out.begin() + static_cast<long>(produced) * kChannels, m_out.begin() + static_cast<long>(frames) * kChannels, 0.0f);
        if (m_state == StreamState::playing) {
            // Hard underrun inside the block (should be rare: low-water fades first).
            m_meters.underruns.fetch_add(1, std::memory_order_relaxed);
            m_afterFade = StreamState::refilling;
            m_state = StreamState::fadingOut;
        }
    }

    // Envelope: fade-in while playing until 1, fade-out until 0.
    const float fadeStep = static_cast<float>(frames / (kFadeSeconds * deviceSampleRate));
    float fadeTarget = m_fade;
    if (m_state == StreamState::playing) {
        fadeTarget = std::min(1.0f, m_fade + fadeStep);
    } else if (m_state == StreamState::fadingOut) {
        fadeTarget = std::max(0.0f, m_fade - fadeStep);
    }

    const float sender = m_params.ignoreSenderGain.load(std::memory_order_relaxed) ? 1.0f : m_params.senderGain.load(std::memory_order_relaxed);
    const float user = m_params.userGain.load(std::memory_order_relaxed);
    const float muteGate = m_params.mute.load(std::memory_order_relaxed) ? 0.0f : 1.0f;
    const float heldGate = m_params.held.load(std::memory_order_relaxed) ? 0.0f : 1.0f;
    const float targetGain = sender * user * muteGate * heldGate * soloGate * fadeTarget;

    applyGainAndMix(left, right, frames, targetGain);
    m_fade = fadeTarget;

    if (m_state == StreamState::fadingOut && m_fade <= 0.0f) {
        finishFadeOut();
    }
    m_meters.state.store(static_cast<uint8_t>(m_state), std::memory_order_relaxed);
}

void SourceStream::topUpInput(uint32_t wantFrames) noexcept
{
    // Only what this block needs: libsamplerate copies everything it is
    // offered into its own buffer, which would hide the ring's fill level
    // from the drift loop and the low-water check.
    const uint32_t capacity = static_cast<uint32_t>(m_in.size() / kChannels);
    const uint32_t limit = std::min(capacity, wantFrames);
    if (m_inFrames >= limit) {
        return;
    }
    const uint32_t room = limit - m_inFrames;
    const uint32_t got = ring().read(std::span<int16_t>(m_s16.data(), static_cast<size_t>(room) * kChannels));
    float* dst = m_in.data() + static_cast<size_t>(m_inFrames) * kChannels;
    for (size_t i = 0; i < static_cast<size_t>(got) * kChannels; ++i) {
        dst[i] = static_cast<float>(m_s16[i]) * kInt16Scale;
    }
    m_inFrames += got;
}

uint32_t SourceStream::produce(uint32_t frames, double ratio) noexcept
{
    uint32_t produced = 0;
    for (int attempt = 0; attempt < 8 && produced < frames; ++attempt) {
        const uint32_t want = static_cast<uint32_t>(std::ceil((frames - produced) / ratio)) + kLookahead;
        topUpInput(want);
        if (m_inFrames == 0) {
            break;
        }
        const uint32_t offer = std::min(m_inFrames, want);
        const auto p = m_resampler.process(
            std::span<const float>(m_in.data(), static_cast<size_t>(offer) * kChannels),
            std::span<float>(m_out.data() + static_cast<size_t>(produced) * kChannels, static_cast<size_t>(frames - produced) * kChannels),
            ratio);
        if (p.framesConsumed > 0) {
            const uint32_t remaining = m_inFrames - p.framesConsumed;
            std::memmove(m_in.data(), m_in.data() + static_cast<size_t>(p.framesConsumed) * kChannels,
                         static_cast<size_t>(remaining) * kChannels * sizeof(float));
            m_inFrames = remaining;
        }
        produced += p.framesProduced;
        if (p.framesConsumed == 0 && p.framesProduced == 0) {
            break;
        }
    }
    return produced;
}

void SourceStream::applyGainAndMix(float* left, float* right, uint32_t frames, float targetGain) noexcept
{
    const float start = m_gain;
    const float step = frames > 0 ? (targetGain - start) / static_cast<float>(frames) : 0.0f;
    float g = start;
    float peakL = 0.0f;
    float peakR = 0.0f;
    double sumSq = 0.0;
    for (uint32_t i = 0; i < frames; ++i) {
        g += step;
        const float l = m_out[static_cast<size_t>(i) * kChannels] * g;
        const float r = m_out[static_cast<size_t>(i) * kChannels + 1] * g;
        left[i] += l;
        right[i] += r;
        peakL = std::max(peakL, std::abs(l));
        peakR = std::max(peakR, std::abs(r));
        const float m = 0.5f * (l + r);
        sumSq += static_cast<double>(m) * static_cast<double>(m);
    }
    m_gain = targetGain;
    m_meters.peakL.store(peakL, std::memory_order_relaxed);
    m_meters.peakR.store(peakR, std::memory_order_relaxed);
    m_meters.rms.store(frames > 0 ? static_cast<float>(std::sqrt(sumSq / frames)) : 0.0f, std::memory_order_relaxed);
}

} // namespace tapein::core::dsp
