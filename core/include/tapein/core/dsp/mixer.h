// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/dsp/source_stream.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
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

private:
    struct Retired {
        std::unique_ptr<SourceStream> source;
        uint64_t retiredAtCallback;
    };

    void renderChunk(float* const* outputs, uint32_t numOutputs, uint32_t frames, double sampleRate) noexcept;

    std::vector<std::atomic<SourceStream*>> m_slots;
    std::vector<Retired> m_retired; ///< control thread only
    std::vector<float> m_left; ///< scratch, maxBlockFrames
    std::vector<float> m_right;
    uint32_t m_maxBlock;
    std::atomic<bool> m_audioActive{false};
    MasterMeters m_meters;
};

} // namespace tapein::core::dsp
