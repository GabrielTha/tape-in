// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/cli/wav_sink.h"

#include <chrono>
#include <ctime>

namespace tapein::cli {

namespace {

std::string timestampNow()
{
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y%m%d-%H%M%S", &local);
    return buf;
}

} // namespace

WavSink::WavSink(std::filesystem::path outDir, Logger log) : m_outDir(std::move(outDir)), m_log(std::move(log)) {}

std::string WavSink::sanitize(std::string_view name)
{
    std::string out;
    for (const char c : name) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-'
                        || c == '_';
        out.push_back(ok ? c : '_');
    }
    if (out.empty()) {
        out = "unknown";
    }
    return out;
}

void WavSink::onSessionStarted(const core::session::SessionInfo& info)
{
    const std::string client = info.clientName.empty() ? info.peer.toString() : info.clientName;
    const auto path = m_outDir / (timestampNow() + "-" + sanitize(client) + "-" + std::to_string(info.id) + ".wav");
    auto writer = core::util::WavWriter::open(path, 44100, 2);
    if (!writer.ok()) {
        if (m_log) {
            m_log("session " + std::to_string(info.id) + ": cannot open " + path.string() + ": " + writer.error().message);
        }
        return;
    }
    m_writers.emplace(info.id, std::move(writer.value()));
    m_files.push_back(path);
    if (m_log) {
        m_log("session " + std::to_string(info.id) + " started: '" + client + "' (" + info.userAgent + ", "
              + (info.encrypted ? "encrypted" : "clear") + ", " + (info.pcm ? "L16" : "ALAC") + ") -> " + path.string());
    }
}

void WavSink::onPcm(uint64_t sessionId, const core::buffer::ReleasedBlock& block)
{
    const auto it = m_writers.find(sessionId);
    if (it == m_writers.end()) {
        return;
    }
    if (auto r = it->second.write(block.pcm); !r.ok() && m_log) {
        m_log("session " + std::to_string(sessionId) + ": write failed: " + r.error().message);
    }
}

void WavSink::onVolume(uint64_t sessionId, float dB)
{
    if (m_log) {
        m_log("session " + std::to_string(sessionId) + " volume " + std::to_string(dB) + " dB");
    }
}

void WavSink::onSessionEnded(uint64_t sessionId, std::string_view reason, const core::session::SessionStats& stats)
{
    const auto it = m_writers.find(sessionId);
    uint64_t frames = 0;
    if (it != m_writers.end()) {
        frames = it->second.framesWritten();
        (void)it->second.close();
        m_writers.erase(it);
    }
    if (m_log) {
        m_log("session " + std::to_string(sessionId) + " ended (" + std::string(reason) + "): "
              + std::to_string(frames) + " frames (" + std::to_string(frames / 44100) + " s), packets "
              + std::to_string(stats.packetsReceived) + ", resends " + std::to_string(stats.resendRepliesReceived)
              + "/" + std::to_string(stats.resendRequestsSent) + ", missing " + std::to_string(stats.packetsMissing)
              + ", decode errors " + std::to_string(stats.decodeErrors) + ", sender latency "
              + std::to_string(stats.senderLatencyFrames) + " frames");
    }
}

} // namespace tapein::cli
