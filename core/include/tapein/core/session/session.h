// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/buffer/jitter_buffer.h"
#include "tapein/core/codec/alac_decoder.h"
#include "tapein/core/crypto/aes_cbc.h"
#include "tapein/core/net/reactor.h"
#include "tapein/core/net/socket.h"
#include "tapein/core/rtp/rtp_packets.h"
#include "tapein/core/rtsp/sdp.h"
#include "tapein/core/session/pcm_sink.h"
#include "tapein/core/util/result.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tapein::core::session {

struct SessionConfig {
    uint16_t holdBackPackets = 63;
    std::chrono::milliseconds releaseInterval{10};
    std::chrono::milliseconds resendFirstAfter{100};
    std::chrono::milliseconds resendRepeatEvery{250};
    /// Ends the session when neither audio nor RTSP activity was seen for
    /// this long (a paused sender keeps its RTSP connection alive).
    std::chrono::milliseconds inactivityTimeout{120000};
    /// Ends a session that was announced but never reached RECORD.
    std::chrono::milliseconds setupTimeout{10000};
    /// After this many consecutive rejected packets (sequence far from the
    /// release point) the jitter buffer resynchronises on the next packet.
    int resyncAfterRejections = 8;
    /// Optional tap for every received datagram: socketKind 0 = audio,
    /// 1 = control, 2 = timing.
    std::function<void(uint64_t sessionId, uint8_t socketKind, std::span<const uint8_t>)> captureRtp;
    /// Called when the session ended on its own (timeouts), after the sink
    /// was told; the owner should drop the session's RTSP connection.
    std::function<void(uint64_t sessionId)> onSelfEnded;
};

/// One audio stream: three UDP sockets, decryption, decoding, the jitter
/// buffer and its timers. Created at ANNOUNCE, armed at SETUP, started at
/// RECORD, ended by TEARDOWN / connection loss / replacement / timeout.
class Session {
public:
    using Logger = std::function<void(std::string)>;

    Session(net::Reactor& reactor, SessionInfo info, const rtsp::AnnounceParams& params,
            std::optional<std::array<uint8_t, 16>> aesKey, IPcmSink& sink, SessionConfig config, Logger log);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    /// Binds the audio, control and timing sockets on localAddress (port 0)
    /// and remembers where the client listens.
    Result<void> setup(const net::Endpoint& localAddress, uint16_t clientControlPort, uint16_t clientTimingPort);
    [[nodiscard]] uint16_t serverPort() const noexcept { return m_serverPort; }
    [[nodiscard]] uint16_t controlPort() const noexcept { return m_controlPort; }
    [[nodiscard]] uint16_t timingPort() const noexcept { return m_timingPort; }

    /// RECORD: notifies the sink and starts the release, resend, timing and
    /// inactivity timers. Idempotent.
    void record();
    [[nodiscard]] bool started() const noexcept { return m_started; }
    [[nodiscard]] bool isSetUp() const noexcept { return m_setUp; }

    /// RTSP traffic on the owning connection counts as activity.
    void noteActivity() noexcept { m_lastActivityAt = std::chrono::steady_clock::now(); }

    /// FLUSH: drops buffered audio before rtpTimestamp.
    void flush(uint32_t rtpTimestamp);

    void setVolume(float dB);
    [[nodiscard]] float volume() const noexcept { return m_volumeDb; }

    /// Releases everything still buffered, closes the sockets, cancels the
    /// timers and notifies the sink (if record() had been called). Idempotent.
    void end(std::string_view reason);
    [[nodiscard]] bool ended() const noexcept { return m_ended; }

    [[nodiscard]] const SessionInfo& info() const noexcept { return m_info; }
    [[nodiscard]] const SessionStats& stats() const noexcept { return m_stats; }

private:
    void onAudioReadable();
    void onControlReadable();
    void onTimingReadable();
    void handleAudio(const rtp::AudioPacket& packet);
    void onReleaseTick();
    void sendTimingRequest();
    void onInactivityTick();
    void onSetupTimeout();
    void endSelf(std::string_view reason);
    void releaseBlock(const buffer::ReleasedBlock& block);
    void drain(net::UdpSocket& socket, uint8_t socketKind,
               void (Session::*handler)(std::span<const uint8_t>, const net::Endpoint&));
    void onAudioDatagram(std::span<const uint8_t> datagram, const net::Endpoint& from);
    void onControlDatagram(std::span<const uint8_t> datagram, const net::Endpoint& from);
    void onTimingDatagram(std::span<const uint8_t> datagram, const net::Endpoint& from);

    net::Reactor& m_reactor;
    SessionInfo m_info;
    rtsp::AnnounceParams m_params;
    IPcmSink& m_sink;
    SessionConfig m_config;
    Logger m_log;

    std::unique_ptr<crypto::AesCbcDecryptor> m_decryptor;
    std::optional<codec::AlacDecoder> m_decoder;
    buffer::JitterBuffer m_jitter;
    std::vector<uint8_t> m_packetScratch; ///< decrypted payload
    std::vector<int16_t> m_pcmScratch; ///< one packet of decoded frames
    std::array<uint8_t, rtp::kMaxDatagram> m_datagram{};

    net::UdpSocket m_audioSocket;
    net::UdpSocket m_controlSocket;
    net::UdpSocket m_timingSocket;
    uint16_t m_serverPort = 0;
    uint16_t m_controlPort = 0;
    uint16_t m_timingPort = 0;
    net::Endpoint m_clientControl;
    net::Endpoint m_clientTiming;

    net::Reactor::TimerId m_releaseTimer = 0;
    net::Reactor::TimerId m_timingTimer = 0;
    net::Reactor::TimerId m_inactivityTimer = 0;
    net::Reactor::TimerId m_setupTimer = 0;
    int m_timingRequestsSent = 0;
    uint16_t m_resendRequestSeq = 0;
    int m_consecutiveRejections = 0;
    std::chrono::steady_clock::time_point m_lastActivityAt{};

    SessionStats m_stats;
    float m_volumeDb = 0.0f;
    bool m_setUp = false;
    bool m_started = false;
    bool m_ended = false;
    bool m_warnedFrameCount = false;
};

} // namespace tapein::core::session
