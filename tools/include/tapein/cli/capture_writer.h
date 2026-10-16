// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <map>
#include <span>

namespace tapein::cli {

/// Records raw traffic for later replay and as fuzz seeds:
///   rtsp-<connection>.log  text, each chunk prefixed by "<<< " (from the
///                          sender) or ">>> " (our reply) and a newline
///   rtp-<session>.bin      records of [kind:1][length:4 big-endian][bytes]
///                          where kind is 0 audio, 1 control, 2 timing
class CaptureWriter {
public:
    enum class Socket : uint8_t { audio = 0, control = 1, timing = 2 };

    explicit CaptureWriter(std::filesystem::path dir);
    ~CaptureWriter();
    CaptureWriter(const CaptureWriter&) = delete;
    CaptureWriter& operator=(const CaptureWriter&) = delete;

    void rtsp(uint64_t connectionId, bool outbound, std::span<const uint8_t> bytes);
    void rtp(uint64_t sessionId, Socket socket, std::span<const uint8_t> datagram);
    void close(uint64_t sessionId);
    void closeAll();

private:
    std::FILE* open(const std::map<uint64_t, std::FILE*>::key_type key, std::map<uint64_t, std::FILE*>& files,
                    const char* prefix, const char* extension);

    std::filesystem::path m_dir;
    std::map<uint64_t, std::FILE*> m_rtspFiles;
    std::map<uint64_t, std::FILE*> m_rtpFiles;
};

} // namespace tapein::cli
