// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace tapein::core::session {

/// Now-playing text pushed by the sender over RTSP SET_PARAMETER as a
/// DMAP/DAAP-tagged blob. Strings are UTF-8, bounded; any field may be empty.
struct DmapMetadata {
    std::string trackName;
    std::string artist;
    std::string album;
};

/// Playback position from the sender's `progress:` line, in RTP frames at
/// the stream sample rate. elapsed = current - start; duration = end - start.
struct DmapProgress {
    uint32_t startFrames = 0;
    uint32_t currentFrames = 0;
    uint32_t endFrames = 0;
};

/// Parses a DMAP-tagged metadata body (sequence of `code[4]` + u32be length +
/// payload; recurses one level into the container codes mlit/mlcl/mshl).
/// Every length is bounds-checked; a tag that overruns the buffer ends the
/// scan. Known string tags minm/asar/asal are captured, clamped to 256 bytes
/// on a UTF-8 code-point boundary. Hostile input never reads out of bounds.
[[nodiscard]] DmapMetadata parseDmapMetadata(std::span<const uint8_t> body);

/// Parses `"progress: <start>/<cur>/<end>"` (the `progress:` prefix is
/// optional). Returns nullopt unless exactly three unsigned integers parse.
[[nodiscard]] std::optional<DmapProgress> parseProgress(std::string_view line);

} // namespace tapein::core::session
