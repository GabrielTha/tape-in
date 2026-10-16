// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/session/pcm_sink.h"
#include "tapein/core/util/wav_writer.h"

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace tapein::cli {

/// Writes each session to its own 44.1 kHz / 16-bit / stereo WAV file:
/// `<outDir>/<YYYYMMDD-HHMMSS>-<client name>-<session id>.wav`.
class WavSink final : public core::session::IPcmSink {
public:
    using Logger = std::function<void(std::string)>;

    WavSink(std::filesystem::path outDir, Logger log);

    void onSessionStarted(const core::session::SessionInfo& info) override;
    void onPcm(uint64_t sessionId, const core::buffer::ReleasedBlock& block) override;
    void onVolume(uint64_t sessionId, float dB) override;
    void onSessionEnded(uint64_t sessionId, std::string_view reason, const core::session::SessionStats& stats) override;

    [[nodiscard]] const std::vector<std::filesystem::path>& files() const noexcept { return m_files; }

    /// "Test iPhone" -> "Test_iPhone"; anything outside [A-Za-z0-9._-] becomes '_'.
    static std::string sanitize(std::string_view name);

private:
    std::filesystem::path m_outDir;
    Logger m_log;
    std::map<uint64_t, core::util::WavWriter> m_writers;
    std::vector<std::filesystem::path> m_files;
};

} // namespace tapein::cli
