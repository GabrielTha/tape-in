// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/crypto/apple_challenge.h"
#include "tapein/core/crypto/raop_key.h"
#include "tapein/core/net/reactor.h"
#include "tapein/core/net/socket.h"
#include "tapein/core/platform/service_advertiser.h"
#include "tapein/core/rtsp/rtsp_message.h"
#include "tapein/core/session/pcm_sink.h"
#include "tapein/core/session/rtsp_connection.h"
#include "tapein/core/session/session.h"
#include "tapein/core/util/result.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace tapein::core::session {

struct ReceiverConfig {
    std::string name = "Tape In";
    crypto::HardwareId hardwareId{0x02, 0x54, 0x41, 0x50, 0x45, 0x01};
    net::Endpoint bindAddress; ///< default: 0.0.0.0, port 0 (dynamic)
    bool alsoBindIpv6 = true; ///< try [::] on the same port
    size_t maxSessions = 100;
    uint16_t holdBackPackets = 63; ///< ≈ 500 ms at 352 frames
    std::string sourceUrl = "https://github.com/tapein/tapein";
    std::string txtEt = "0,1"; ///< experiment hook: "0" advertises no encryption
    bool answerAppleChallenge = true; ///< experiment hook
    std::function<void(std::string)> log; ///< optional, network thread
    /// Optional raw-traffic taps for the capture tool (network thread).
    std::function<void(uint64_t connectionId, bool outbound, std::span<const uint8_t>)> captureRtsp;
    std::function<void(uint64_t sessionId, uint8_t socketKind, std::span<const uint8_t>)> captureRtp;
};

/// One advertised RAOP receiver: the RTSP listener, its connections and
/// sessions, and the Bonjour record. Lives on the reactor thread.
class Receiver {
public:
    Receiver(net::Reactor& reactor, ReceiverConfig config, IPcmSink& sink, platform::IServiceAdvertiser& advertiser,
             std::shared_ptr<crypto::RaopRsaKey> key);
    ~Receiver();
    Receiver(const Receiver&) = delete;
    Receiver& operator=(const Receiver&) = delete;

    Result<void> start();
    void stop();

    [[nodiscard]] uint16_t rtspPort() const noexcept { return m_port; }
    [[nodiscard]] platform::ServiceRecord serviceRecord() const;
    [[nodiscard]] size_t sessionCount() const noexcept;
    [[nodiscard]] SessionStats sessionStats(uint64_t sessionId) const;
    [[nodiscard]] const ReceiverConfig& config() const noexcept { return m_config; }

private:
    void onAccept(net::TcpListener& listener);
    void onRequest(RtspConnection& conn, const rtsp::Request& request);
    void onConnectionClosed(RtspConnection& conn, std::string_view reason);
    rtsp::Response makeResponse(const RtspConnection& conn, const rtsp::Request& request, int status) const;

    void handleOptions(RtspConnection&, const rtsp::Request&);
    void handleAnnounce(RtspConnection&, const rtsp::Request&);
    void handleSetup(RtspConnection&, const rtsp::Request&);
    void handleRecord(RtspConnection&, const rtsp::Request&);
    void handleFlush(RtspConnection&, const rtsp::Request&);
    void handleTeardown(RtspConnection&, const rtsp::Request&);
    void handleSetParameter(RtspConnection&, const rtsp::Request&);
    void handleGetParameter(RtspConnection&, const rtsp::Request&);

    Session* findSession(uint64_t id) noexcept;
    void endSession(uint64_t id, std::string_view reason);
    void scheduleCleanup();
    void cleanup();
    void log(std::string message) const;

    net::Reactor& m_reactor;
    ReceiverConfig m_config;
    IPcmSink& m_sink;
    platform::IServiceAdvertiser& m_advertiser;
    std::shared_ptr<crypto::RaopRsaKey> m_key;

    net::TcpListener m_listener4;
    net::TcpListener m_listener6;
    uint16_t m_port = 0;
    bool m_running = false;
    bool m_cleanupScheduled = false;

    std::vector<std::unique_ptr<RtspConnection>> m_connections;
    std::vector<std::unique_ptr<Session>> m_sessions;
    uint64_t m_nextConnectionId = 1;
    uint64_t m_nextSessionId = 1;
};

} // namespace tapein::core::session
