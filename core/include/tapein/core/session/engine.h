// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/crypto/raop_key.h"
#include "tapein/core/dsp/mixer.h"
#include "tapein/core/dsp/source_stream.h"
#include "tapein/core/net/reactor.h"
#include "tapein/core/platform/service_advertiser.h"
#include "tapein/core/session/pcm_sink.h"
#include "tapein/core/session/receiver.h"
#include "tapein/core/util/result.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace tapein::core::session {

/// How much audio the receiver holds before playing, trading latency for
/// robustness. Default is sender sync (≈ 2 s, auxCord parity).
enum class BufferMode { senderSync, shortBuffer, monitoring };

struct EngineConfig {
    ReceiverConfig receiver;
    BufferMode bufferMode = BufferMode::senderSync;
    bool ignoreSenderVolume = false;
    dsp::ResamplerQuality quality = dsp::ResamplerQuality::medium;
    std::chrono::milliseconds lingerTime{30000};
    uint32_t ringFrames = 262144; ///< ≈ 5.9 s at 44.1 kHz, power of two
};

/// A snapshot of one source for the UI. Values are copied under the
/// registry lock; never read on the audio thread.
struct SourceSnapshot {
    uint64_t sourceId = 0;
    std::string clientName;
    std::string dacpId;
    std::string activeRemote;
    std::string peer;
    std::string userAgent;
    bool live = false;
    dsp::StreamState state = dsp::StreamState::buffering;
    float peakL = 0.0f;
    float peakR = 0.0f;
    float rms = 0.0f;
    uint32_t fillFrames = 0;
    uint32_t targetFillFrames = 0;
    uint32_t underruns = 0;
    float senderVolumeDb = 0.0f;
    float userGainDb = 0.0f;
    bool mute = false;
    bool solo = false;
    float ratioDeviation = 0.0f;
    SessionStats stats;
    // Now-playing (Phase 3), pushed by the sender.
    std::string trackName;
    std::string artist;
    std::string album;
    bool hasArtwork = false;
    std::vector<uint8_t> artwork;
    std::string artworkMime;
    uint32_t elapsedSeconds = 0;
    uint32_t durationSeconds = 0;
};

struct EngineEvent {
    enum class Kind { sourceAdded, sourceChanged, sourceRemoved, receiverStarted, receiverError, log };
    Kind kind;
    uint64_t sourceId = 0;
    std::string text;
};

/// Linear gain for the RAOP volume in dB; −144 (and below) is mute.
float raopVolumeDbToGain(float dB) noexcept;

/// Owns the receiver (network thread), the mixer (audio thread pulls it),
/// and the source registry keyed by DACP-ID. Implements IPcmSink: a session
/// becomes a source, its released PCM feeds the source's ring, and session
/// end makes the source linger before removal.
class Engine final : public IPcmSink {
public:
    /// externalReactor != nullptr: the caller runs the reactor (tests).
    /// nullptr: the engine owns a network thread between start() and stop().
    Engine(EngineConfig config, platform::IServiceAdvertiser& advertiser, std::shared_ptr<crypto::RaopRsaKey> key,
           net::Reactor* externalReactor = nullptr);
    ~Engine() override;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    Result<void> start();
    void stop();

    void setName(std::string name);
    void setBufferMode(BufferMode mode);
    void setIgnoreSenderVolume(bool ignore);

    void setUserGainDb(uint64_t sourceId, float dB);
    void setMute(uint64_t sourceId, bool mute);
    void setSolo(uint64_t sourceId, bool solo);

    [[nodiscard]] std::vector<SourceSnapshot> sources() const;
    [[nodiscard]] std::vector<EngineEvent> pollEvents();

    [[nodiscard]] dsp::Mixer& mixer() noexcept { return m_mixer; }
    void setAudioActive(bool active);

    [[nodiscard]] std::string name() const;
    [[nodiscard]] uint16_t rtspPort() const;
    [[nodiscard]] std::string lastError() const;

private:
    // IPcmSink (network thread)
    void onSessionStarted(const SessionInfo& info) override;
    void onPcm(uint64_t sessionId, const buffer::ReleasedBlock& block) override;
    void onVolume(uint64_t sessionId, float dB) override;
    void onSenderLatency(uint64_t sessionId, uint32_t frames) override;
    void onMetadata(uint64_t sessionId, const DmapMetadata& metadata) override;
    void onArtwork(uint64_t sessionId, std::span<const uint8_t> image, std::string_view mime) override;
    void onProgress(uint64_t sessionId, const DmapProgress& progress) override;
    void onSessionEnded(uint64_t sessionId, std::string_view reason, const SessionStats& stats) override;

    struct Source {
        uint64_t sourceId = 0;
        std::string dacpId;
        uint64_t sessionId = 0;
        uint32_t slot = UINT32_MAX;
        dsp::SourceStream* stream = nullptr; ///< owned by the mixer; touched only under m_mutex (never the audio thread)
        bool live = false;
        float userGainDb = 0.0f;
        bool mute = false;
        bool solo = false;
        float senderVolumeDb = 0.0f;
        uint32_t senderLatencyFrames = 0;
        SessionInfo info;
        SessionStats stats;
        DmapMetadata metadata;
        std::vector<uint8_t> artwork;
        std::string artworkMime;
        DmapProgress progress;
        net::Reactor::TimerId lingerTimer = 0;
    };

    [[nodiscard]] uint16_t holdBackPacketsFor(BufferMode mode) const noexcept;
    [[nodiscard]] uint32_t targetFramesFor(BufferMode mode, uint32_t senderLatencyFrames) const noexcept;
    void applyTarget(Source& src); ///< recomputes the stream target from mode + latency
    void runOnReactor(std::function<void()> fn); ///< inline (external reactor) or marshalled (owned thread)
    void rebuildReceiver(); ///< on the reactor thread
    Source* bySession(uint64_t sessionId) noexcept; ///< m_mutex held
    Source* byId(uint64_t sourceId) noexcept; ///< m_mutex held
    void scheduleGc();
    void refreshStats(); ///< reactor thread: copy live session stats into the registry
    void emit(EngineEvent event); ///< m_mutex held

    EngineConfig m_config;
    platform::IServiceAdvertiser& m_advertiser;
    std::shared_ptr<crypto::RaopRsaKey> m_key;

    net::Reactor* m_reactor;
    bool m_ownsReactor;
    std::unique_ptr<net::Reactor> m_ownReactor;
    std::thread m_thread;

    dsp::Mixer m_mixer;
    std::unique_ptr<Receiver> m_receiver;

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, std::unique_ptr<Source>> m_sources; ///< by DACP-ID
    std::unordered_map<uint64_t, Source*> m_bySession;
    std::vector<EngineEvent> m_events;
    uint64_t m_nextSourceId = 1;
    std::string m_name;
    std::string m_lastError;
    bool m_running = false;
    bool m_gcScheduled = false;
    net::Reactor::TimerId m_statsTimer = 0;
};

} // namespace tapein::core::session
