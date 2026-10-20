// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/crypto/raop_key.h"
#include "tapein/core/dsp/mixer.h"
#include "tapein/core/dsp/source_stream.h"
#include "tapein/core/net/reactor.h"
#include "tapein/core/platform/service_advertiser.h"
#include "tapein/core/platform/service_resolver.h"
#include "tapein/core/session/dacp_client.h"
#include "tapein/core/session/pcm_sink.h"
#include "tapein/core/session/receiver.h"
#include "tapein/core/util/result.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
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
    bool exclusiveSolo = false; ///< soloing one source clears solo on the others
    bool askBeforeAccept = false; ///< hold a new connection silent until the user allows it
    dsp::ResamplerQuality quality = dsp::ResamplerQuality::medium;
    std::chrono::milliseconds lingerTime{30000};
    uint32_t ringFrames = 262144; ///< ≈ 5.9 s at 44.1 kHz, power of two
};

/// A snapshot of one source for the UI. Values are copied under the
/// registry lock; never read on the audio thread.
struct SourceSnapshot {
    uint64_t sourceId = 0;
    bool pending = false; ///< awaiting the user's allow/decline (held silent)
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

/// Audio captured by a manual recording: the clean (pre-fader) mix of every
/// live source, summed on the reactor thread.
struct RecordedSample {
    std::vector<int16_t> interleaved; ///< stereo, interleaved
    uint32_t sampleRate = 44100;
    uint32_t channels = 2;
    std::string trackName; ///< now-playing snapshot at stop, for naming
    std::string artist;
    std::string album;
};

/// Live state of the single, global recording, for the UI.
struct RecordStatus {
    bool recording = false;
    uint32_t frames = 0; ///< frames captured so far (interleaved size / channels)
};

/// Safety cap on a manual recording (auto-stops to bound memory).
inline constexpr uint32_t kMaxRecordSeconds = 60;

/// Number of envelope bins the input-waveform preview keeps (oldest → newest).
inline constexpr size_t kWaveformBins = 256;

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
    /// Builds the DACP resolver bound to the engine's reactor. Supplied by the
    /// app (a platform impl); nullptr disables remote control.
    using ResolverFactory = std::function<std::unique_ptr<platform::IServiceResolver>(net::Reactor&)>;

    /// externalReactor != nullptr: the caller runs the reactor (tests).
    /// nullptr: the engine owns a network thread between start() and stop().
    Engine(EngineConfig config, platform::IServiceAdvertiser& advertiser, std::shared_ptr<crypto::RaopRsaKey> key,
           net::Reactor* externalReactor = nullptr, ResolverFactory resolverFactory = nullptr);
    ~Engine() override;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    Result<void> start();
    void stop();

    void setName(std::string name);
    void setBufferMode(BufferMode mode);
    void setIgnoreSenderVolume(bool ignore);
    /// "Restrict access": advertise and require an RTSP password (empty
    /// password disables it even when enabled). Rebuilds the receiver.
    void setRequirePassword(bool require, std::string password);
    /// When true, enabling solo on one source clears it on the others.
    void setExclusiveSolo(bool exclusive);
    /// When true, a new connection is accepted but held silent until the user
    /// approves it; affects only sessions that start after this is set.
    void setAskBeforeAccept(bool ask);
    /// Release a pending source: its audio flows and `pending` clears.
    void approveConnection(uint64_t sourceId);
    /// Reject a pending source: tear down its RTSP session and remove it now.
    void declineConnection(uint64_t sourceId);

    /// Manual sampling (global): capture the clean (pre-fader) mix of every
    /// live source. start clears and arms; stop disarms and returns the capture
    /// (empty if nothing was recording). Auto-stops at kMaxRecordSeconds.
    void startRecording();
    [[nodiscard]] RecordedSample stopRecording();
    [[nodiscard]] RecordStatus recordStatus() const;
    /// A rolling envelope (0..1 peaks, oldest → newest) of the recent input,
    /// for the record bar's live waveform; maintained whether or not recording.
    [[nodiscard]] std::vector<float> inputWaveform() const;

    void setUserGainDb(uint64_t sourceId, float dB);
    void setMute(uint64_t sourceId, bool mute);
    void setSolo(uint64_t sourceId, bool solo);

    /// Remote control sent back to the sender over DACP. A no-op for a source
    /// that is not live, has no Active-Remote, or when no resolver was supplied.
    void sendTransportCommand(uint64_t sourceId, TransportCommand cmd);
    void setSenderVolumeDb(uint64_t sourceId, float dB);

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
        bool pending = false; ///< awaiting allow/decline; held silent until approved
        size_t recordPos = 0;     ///< write cursor (interleaved samples) into m_recordMix for the current recording
        bool recordJoined = false; ///< recordPos initialised for the current recording
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

    // Global sampling (reactor thread / under m_mutex). m_recordMix accumulates
    // the summed clean PCM as int32 (headroom for mixing) until stop clamps it
    // to int16. m_waveform is a rolling peak envelope for the live preview.
    bool m_recording = false;
    std::vector<int32_t> m_recordMix; ///< interleaved stereo mix accumulator
    std::array<float, kWaveformBins> m_waveform{};
    size_t m_waveformHead = 0; ///< bin currently filling
    std::chrono::steady_clock::time_point m_waveformTime{}; ///< start of the filling bin
    bool m_running = false;
    bool m_gcScheduled = false;
    net::Reactor::TimerId m_statsTimer = 0;

    // Remote control. Declared last so they destruct before the reactor they
    // were bound to; m_dacp holds a reference into m_resolver, so it is last.
    std::unique_ptr<platform::IServiceResolver> m_resolver;
    std::unique_ptr<DacpClient> m_dacp;
};

} // namespace tapein::core::session
