// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/util/result.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <span>

namespace tapein::core::util {

/// Minimal 16-bit PCM WAV writer. The header is written with placeholder
/// sizes and patched on close() (or in the destructor).
class WavWriter {
public:
    static Result<WavWriter> open(const std::filesystem::path& path, uint32_t sampleRate, uint16_t channels);

    WavWriter(WavWriter&& other) noexcept;
    WavWriter& operator=(WavWriter&& other) noexcept;
    WavWriter(const WavWriter&) = delete;
    WavWriter& operator=(const WavWriter&) = delete;
    ~WavWriter();

    /// Appends interleaved samples.
    Result<void> write(std::span<const int16_t> interleaved);
    /// Patches the header and closes the file. Safe to call twice.
    Result<void> close();

    [[nodiscard]] uint64_t framesWritten() const noexcept { return m_frames; }

private:
    WavWriter(std::FILE* file, uint32_t sampleRate, uint16_t channels) noexcept;
    Result<void> writeHeader();

    std::FILE* m_file = nullptr;
    uint32_t m_sampleRate = 0;
    uint16_t m_channels = 0;
    uint64_t m_frames = 0;
};

} // namespace tapein::core::util
