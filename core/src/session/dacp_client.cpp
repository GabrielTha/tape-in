// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/session/dacp_client.h"

#include "tapein/core/net/socket.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <span>

namespace tapein::core::session {

std::string dacpPath(TransportCommand cmd)
{
    switch (cmd) {
    case TransportCommand::playPause:
        return "/ctrl-int/1/playpause";
    case TransportCommand::next:
        return "/ctrl-int/1/nextitem";
    case TransportCommand::previous:
        return "/ctrl-int/1/previtem";
    }
    return "/ctrl-int/1/playpause";
}

std::string dacpVolumePath(float dB)
{
    const float clamped = std::clamp(dB, -30.0f, 0.0f);
    std::array<char, 16> buf{};
    std::snprintf(buf.data(), buf.size(), "%.4f", clamped);
    return std::string("/ctrl-int/1/setproperty?dmcp.device-volume=") + buf.data();
}

std::string buildDacpGet(std::string_view path, std::string_view host, uint16_t port, std::string_view activeRemote)
{
    std::string req;
    req.reserve(128);
    req += "GET ";
    req += path;
    req += " HTTP/1.1\r\nHost: ";
    req += host;
    req += ':';
    req += std::to_string(port);
    req += "\r\nActive-Remote: ";
    req += activeRemote;
    req += "\r\nConnection: close\r\n\r\n";
    return req;
}

DacpClient::DacpClient(platform::IServiceResolver& resolver) : m_resolver(resolver) {}

void DacpClient::send(std::string dacpId, std::string activeRemote, std::string path)
{
    const std::string instance = "iTunes_Ctrl_" + dacpId;
    m_resolver.resolve(instance, "_dacp._tcp",
                       [path = std::move(path), activeRemote = std::move(activeRemote)](
                           core::Result<platform::ResolvedService> result) {
                           if (!result.ok()) {
                               return; // sender not found; drop the command
                           }
                           const auto& svc = result.value();
                           const std::string full = svc.endpoint.toString();
                           const std::string host = full.substr(0, full.rfind(':'));

                           // Bounded connect so a resolvable-but-dead sender
                           // (phone asleep) never stalls the reactor thread.
                           auto sock = net::TcpSocket::connect(svc.endpoint, std::chrono::milliseconds(300));
                           if (!sock.ok()) {
                               return; // sender unreachable; drop the command
                           }
                           const std::string req = buildDacpGet(path, host, svc.endpoint.port, activeRemote);
                           std::span<const uint8_t> remaining(reinterpret_cast<const uint8_t*>(req.data()), req.size());
                           while (!remaining.empty()) {
                               auto written = sock.value().write(remaining);
                               if (!written.ok()) {
                                   return;
                               }
                               if (written.value() == 0) {
                                   break; // would block; a short control request rarely gets here
                               }
                               remaining = remaining.subspan(written.value());
                           }
                       });
}

} // namespace tapein::core::session
