// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/dsp/source_stream.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace tapein::core::dsp {

struct MasterMeters {
    std::atomic<float> peakL{0.0f};
    std::atomic<float> peakR{0.0f};
    std::atomic<uint64_t> callbacks{0}; ///< render() calls completed
};

/// Sums every attached SourceStream on the audio thread. Sources live in
/// fixed slots holding atomic pointers: the control thread attaches and
/// detaches them without locks, and a detached source is freed only once
/// the audio thread has finished a render() that could have used it.
class Mixer {
public:
    /// Bins of the playback envelope ring (~8 ms of audio each).
    static constexpr size_t kEnvelopeBins = 256;

    explicit Mixer(uint32_t maxSources, uint32_t maxBlockFrames = SourceStream::kDefaultMaxBlock);
    ~Mixer();
    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;

    // control thread
    std::optional<uint32_t> add(std::unique_ptr<SourceStream> source); ///< slot index; nullopt when full
    void remove(uint32_t slot); ///< detaches; memory freed by collectGarbage()
    void collectGarbage(); ///< frees detached sources the audio thread has passed (all of them when audio is inactive)
    void setAudioActive(bool active) noexcept;
    [[nodiscard]] SourceStream* source(uint32_t slot) noexcept; ///< nullptr if empty
    [[nodiscard]] uint32_t maxSources() const noexcept { return static_cast<uint32_t>(m_slots.size()); }
    [[nodiscard]] size_t pendingGarbage() const noexcept { return m_retired.size(); }

    // audio thread
    /// Zero-fills `outputs` then mixes every source into channels 0/1 (a
    /// single channel gets the average), soft-clips, updates the meters.
    void render(float* const* outputs, uint32_t numOutputs, uint32_t frames, double sampleRate) noexcept;
    [[nodiscard]] const MasterMeters& meters() const noexcept { return m_meters; }

    // any thread
    /// Returns the {L, R} peak accumulated since the last call and resets it,
    /// so a slow UI poll still sees the loudest moment between reads.
    [[nodiscard]] std::pair<float, float> consumePeaks() noexcept
    {
        return {m_meters.peakL.exchange(0.0f, std::memory_order_relaxed),
                m_meters.peakR.exchange(0.0f, std::memory_order_relaxed)};
    }

    /// Copies the rolling envelope of the rendered mix (0..1 peaks, oldest →
    /// newest) into `out`. Driven by the audio clock, so it scrolls steadily
    /// no matter how the network delivered the audio.
    void playbackEnvelope(float* out, size_t bins) const noexcept;

private:
    struct Retired {
        std::unique_ptr<SourceStream> source;
        uint64_t retiredAtCallback;
    };

    void renderChunk(float* const* outputs, uint32_t numOutputs, uint32_t frames, double sampleRate) noexcept;

    std::vector<std::atomic<SourceStream*>> m_slots;
    std::vector<Retired> m_retired; ///< control thread only
    std::array<std::atomic<float>, kEnvelopeBins> m_envBins{};
    std::atomic<uint64_t> m_envHead{0}; ///< completed envelope bins, ever
    float m_envPeak = 0.0f;   ///< audio thread: peak of the bin being filled
    uint32_t m_envFill = 0;   ///< audio thread: frames into the current bin
    std::vector<float> m_left; ///< scratch, maxBlockFrames
    std::vector<float> m_right;
    uint32_t m_maxBlock;
    std::atomic<bool> m_audioActive{false};
    MasterMeters m_meters;
};

} // namespace tapein::core::dsp
