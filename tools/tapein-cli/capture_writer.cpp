// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/cli/capture_writer.h"

#include <string>

namespace tapein::cli {

CaptureWriter::CaptureWriter(std::filesystem::path dir) : m_dir(std::move(dir))
{
    std::error_code ec;
    std::filesystem::create_directories(m_dir, ec);
}

CaptureWriter::~CaptureWriter()
{
    closeAll();
}

std::FILE* CaptureWriter::open(uint64_t key, std::map<uint64_t, std::FILE*>& files, const char* prefix,
                               const char* extension)
{
    const auto it = files.find(key);
    if (it != files.end()) {
        return it->second;
    }
    const auto path = m_dir / (std::string(prefix) + std::to_string(key) + extension);
    std::FILE* f = std::fopen(path.string().c_str(), "wb");
    if (f != nullptr) {
        files.emplace(key, f);
    }
    return f;
}

void CaptureWriter::rtsp(uint64_t connectionId, bool outbound, std::span<const uint8_t> bytes)
{
    std::FILE* f = open(connectionId, m_rtspFiles, "rtsp-", ".log");
    if (f == nullptr) {
        return;
    }
    std::fputs(outbound ? ">>> " : "<<< ", f);
    std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fputc('\n', f);
    std::fflush(f);
}

void CaptureWriter::rtp(uint64_t sessionId, Socket socket, std::span<const uint8_t> datagram)
{
    std::FILE* f = open(sessionId, m_rtpFiles, "rtp-", ".bin");
    if (f == nullptr) {
        return;
    }
    const uint32_t len = static_cast<uint32_t>(datagram.size());
    const uint8_t header[5] = {static_cast<uint8_t>(socket), static_cast<uint8_t>(len >> 24), static_cast<uint8_t>(len >> 16),
                               static_cast<uint8_t>(len >> 8), static_cast<uint8_t>(len)};
    std::fwrite(header, 1, sizeof header, f);
    std::fwrite(datagram.data(), 1, datagram.size(), f);
}

void CaptureWriter::close(uint64_t sessionId)
{
    const auto it = m_rtpFiles.find(sessionId);
    if (it != m_rtpFiles.end()) {
        std::fclose(it->second);
        m_rtpFiles.erase(it);
    }
}

void CaptureWriter::closeAll()
{
    for (auto& [id, f] : m_rtpFiles) {
        std::fclose(f);
    }
    m_rtpFiles.clear();
    for (auto& [id, f] : m_rtspFiles) {
        std::fclose(f);
    }
    m_rtspFiles.clear();
}

} // namespace tapein::cli
