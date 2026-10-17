// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/buffer/spsc_ring.h"
#include "tapein/core/dsp/drift_controller.h"
#include "tapein/core/dsp/resampler.h"
#include "tapein/core/util/result.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace tapein::core::dsp {

enum class StreamState : uint8_t { buffering, playing, fadingOut, refilling, stopped };

/// Written by the network / UI threads, read on the audio thread.
struct SourceParams {
    std::atomic<float> senderGain{1.0f}; ///< linear, from the RTSP volume
    std::atomic<float> userGain{1.0f}; ///< linear, from the fader
    std::atomic<bool> mute{false};
    std::atomic<bool> solo{false};
    std::atomic<bool> ignoreSenderGain{false};
    std::atomic<uint32_t> targetFillFrames{66150}; ///< ring frames to hold before / while playing
    std::atomic<bool> stopRequested{false}; ///< fade out, then StreamState::stopped
};

/// Written on the audio thread, read anywhere.
struct SourceMeters {
    std::atomic<float> peakL{0.0f};
    std::atomic<float> peakR{0.0f};
    std::atomic<float> rms{0.0f};
    std::atomic<uint32_t> fillFrames{0};
    std::atomic<uint8_t> state{static_cast<uint8_t>(StreamState::buffering)};
    std::atomic<float> ratioDeviation{0.0f};
    std::atomic<uint32_t> underruns{0};
};

/// One session's audio-thread consumer: pulls int16 frames from its ring,
/// resamples them to the device rate with the drift controller steering the
/// ratio, applies sender/user gain, mute and solo with per-block ramps,
/// fades on underrun, resync, flush and stop, and publishes meters. All
/// memory is allocated in create(); render() never allocates, locks or logs.
class SourceStream {
public:
    static constexpr uint32_t kDefaultMaxBlock = 4096;
    static constexpr uint32_t kRingRate = 44100;

    static Result<std::unique_ptr<SourceStream>> create(buffer::HeapRing ring, ResamplerQuality quality,
                                                        uint32_t maxBlockFrames = kDefaultMaxBlock);
    SourceStream(const SourceStream&) = delete;
    SourceStream& operator=(const SourceStream&) = delete;

    [[nodiscard]] buffer::SpscRing& ring() noexcept { return m_ring.ring(); }
    [[nodiscard]] SourceParams& params() noexcept { return m_params; }
    [[nodiscard]] const SourceMeters& meters() const noexcept { return m_meters; }
    [[nodiscard]] uint32_t maxBlockFrames() const noexcept { return m_maxBlock; }

    /// Audio thread. Adds this source into left/right (frames ≤ maxBlockFrames;
    /// larger blocks are processed in chunks). soloGate is 0 when another
    /// source is soloed and this one is not, else 1.
    void render(float* left, float* right, uint32_t frames, double deviceSampleRate, float soloGate) noexcept;

private:
    SourceStream(buffer::HeapRing ring, Resampler resampler, uint32_t maxBlock);

    void renderChunk(float* left, float* right, uint32_t frames, double deviceSampleRate, float soloGate) noexcept;
    void handleRequests(uint32_t fill) noexcept;
    void beginFadeOut(StreamState after) noexcept;
    void finishFadeOut() noexcept;
    void beginPlaying() noexcept;
    uint32_t produce(uint32_t frames, double ratio) noexcept; ///< fills m_out with up to `frames` frames
    void topUpInput(uint32_t wantFrames) noexcept;
    void applyGainAndMix(float* left, float* right, uint32_t frames, float targetGain) noexcept;

    buffer::HeapRing m_ring;
    Resampler m_resampler;
    DriftController m_drift;
    SourceParams m_params;
    SourceMeters m_meters;

    uint32_t m_maxBlock;
    std::vector<int16_t> m_s16; ///< ring read scratch
    std::vector<float> m_in; ///< float input FIFO, interleaved
    uint32_t m_inFrames = 0; ///< frames held in m_in
    std::vector<float> m_out; ///< resampled block, interleaved, before gain

    StreamState m_state = StreamState::buffering;
    StreamState m_afterFade = StreamState::refilling;
    float m_fade = 0.0f; ///< 0..1 envelope for fade-in / fade-out
    float m_gain = 0.0f; ///< last applied gain (ramp start)
    uint32_t m_pendingDiscard = 0;
    double m_lastRate = 0.0;
};

} // namespace tapein::core::dsp
