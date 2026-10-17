// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/util/result.h"

#include <cstdint>
#include <span>

namespace tapein::core::dsp {

enum class ResamplerQuality { fastest, medium, best }; ///< libsamplerate sinc converters

/// Float, interleaved, variable-ratio sinc resampler over libsamplerate.
/// create() is the only call that allocates; process() is safe on the audio
/// thread (libsamplerate's src_process allocates nothing).
class Resampler {
public:
    struct Progress {
        uint32_t framesConsumed = 0;
        uint32_t framesProduced = 0;
    };

    static Result<Resampler> create(uint32_t channels, ResamplerQuality quality);
    Resampler(Resampler&& other) noexcept;
    Resampler& operator=(Resampler&& other) noexcept;
    Resampler(const Resampler&) = delete;
    Resampler& operator=(const Resampler&) = delete;
    ~Resampler();

    /// Forgets the filter history (new stream).
    void reset() noexcept;
    /// Makes the next process() start from this ratio instead of
    /// interpolating from the previous block's (device switch).
    void setRatio(double outOverIn) noexcept;
    /// Converts as much of `in` as fits into `out`; the ratio is
    /// interpolated across the block from the previous call's ratio.
    Progress process(std::span<const float> in, std::span<float> out, double outOverIn) noexcept;

    [[nodiscard]] uint32_t channels() const noexcept { return m_channels; }

private:
    Resampler(void* state, uint32_t channels) noexcept : m_state(state), m_channels(channels) {}

    void* m_state = nullptr; // SRC_STATE*
    uint32_t m_channels = 0;
};

} // namespace tapein::core::dsp
