// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/session/dacp_client.h"

#include <algorithm>
#include <array>
#include <cstdio>

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

} // namespace tapein::core::session
