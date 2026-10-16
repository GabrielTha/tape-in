// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/session/rtsp_connection.h"

#include <array>

namespace tapein::core::session {

RtspConnection::RtspConnection(uint64_t id, net::TcpSocket socket, RequestHandler onRequest, CloseHandler onClose,
                               CaptureHandler capture)
    : m_id(id)
    , m_socket(std::move(socket))
    , m_onRequest(std::move(onRequest))
    , m_onClose(std::move(onClose))
    , m_capture(std::move(capture))
{
}

void RtspConnection::onReadable()
{
    std::array<uint8_t, 4096> buf{};
    for (int iterations = 0; iterations < 16 && !m_closed; ++iterations) {
        auto n = m_socket.read(buf);
        if (!n.ok()) {
            close("connection closed");
            return;
        }
        if (n.value() == 0) {
            return; // nothing more right now
        }
        if (m_capture) {
            m_capture(m_id, false, std::span<const uint8_t>(buf.data(), n.value()));
        }
        auto requests = m_parser.feed(std::span<const uint8_t>(buf.data(), n.value()));
        if (!requests.ok()) {
            close("malformed RTSP: " + requests.error().message);
            return;
        }
        for (const auto& request : requests.value()) {
            if (m_closed) {
                return;
            }
            m_onRequest(*this, request);
        }
    }
}

void RtspConnection::send(const rtsp::Response& response)
{
    if (m_closed) {
        return;
    }
    const auto bytes = response.serialize();
    if (m_capture) {
        m_capture(m_id, true, bytes);
    }
    size_t offset = 0;
    while (offset < bytes.size()) {
        auto n = m_socket.write(std::span<const uint8_t>(bytes.data() + offset, bytes.size() - offset));
        if (!n.ok() || n.value() == 0) {
            close("write failed");
            return;
        }
        offset += n.value();
    }
}

void RtspConnection::close(std::string_view reason)
{
    if (m_closed) {
        return;
    }
    m_closed = true;
    // Notify first, while handle() is still valid, so the owner can drop the
    // reactor registration before the descriptor number can be reused.
    m_onClose(*this, reason);
    m_socket.close();
}

} // namespace tapein::core::session
