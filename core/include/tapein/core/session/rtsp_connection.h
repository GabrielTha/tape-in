// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/net/socket.h"
#include "tapein/core/rtsp/rtsp_message.h"
#include "tapein/core/rtsp/rtsp_parser.h"
#include "tapein/core/rtsp/sdp.h"
#include "tapein/core/util/result.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace tapein::core::session {

/// One RTSP TCP connection: socket I/O and request framing. The protocol
/// itself is handled by the Receiver through the request callback.
class RtspConnection {
public:
    using RequestHandler = std::function<void(RtspConnection&, const rtsp::Request&)>;
    using CloseHandler = std::function<void(RtspConnection&, std::string_view reason)>;
    using CaptureHandler = std::function<void(uint64_t connectionId, bool outbound, std::span<const uint8_t>)>;

    RtspConnection(uint64_t id, net::TcpSocket socket, RequestHandler onRequest, CloseHandler onClose,
                   CaptureHandler capture = nullptr);

    [[nodiscard]] uint64_t id() const noexcept { return m_id; }
    [[nodiscard]] net::NativeHandle handle() const noexcept { return m_socket.handle(); }
    [[nodiscard]] Result<net::Endpoint> localEndpoint() const { return m_socket.localEndpoint(); }
    [[nodiscard]] Result<net::Endpoint> peerEndpoint() const { return m_socket.peerEndpoint(); }
    [[nodiscard]] bool isOpen() const noexcept { return m_socket.isOpen(); }

    /// Reads what is available, parses, and dispatches complete requests.
    void onReadable();
    /// Serialises and writes a response. A short or failed write closes the
    /// connection (responses are small; partial writes mean a dead peer).
    void send(const rtsp::Response& response);
    /// Closes the socket and notifies the owner once.
    void close(std::string_view reason);

    // Per-connection protocol state kept here for the Receiver's convenience.
    uint64_t sessionId = 0; ///< 0 = no session attached
    std::optional<rtsp::AnnounceParams> announce;
    std::optional<std::array<uint8_t, 16>> aesKey;
    std::string dacpId;
    std::string activeRemote;
    std::string userAgent;
    std::string clientName;
    std::string authNonce; ///< the Digest challenge nonce we last issued on this connection

private:
    uint64_t m_id;
    net::TcpSocket m_socket;
    rtsp::Parser m_parser;
    RequestHandler m_onRequest;
    CloseHandler m_onClose;
    CaptureHandler m_capture;
    bool m_closed = false;
};

} // namespace tapein::core::session
