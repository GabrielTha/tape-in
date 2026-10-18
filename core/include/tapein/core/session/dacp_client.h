// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/platform/service_resolver.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace tapein::core::session {

/// Transport commands sent back to the sender over DACP.
enum class TransportCommand { playPause, next, previous };

/// The DACP control path for a transport command, e.g. "/ctrl-int/1/playpause".
[[nodiscard]] std::string dacpPath(TransportCommand cmd);

/// The DACP path that sets the sender's device volume; dB is clamped to
/// [-30, 0] and formatted with four decimals.
[[nodiscard]] std::string dacpVolumePath(float dB);

/// A complete HTTP/1.1 GET for a DACP control path, carrying Host,
/// Active-Remote and Connection: close.
[[nodiscard]] std::string buildDacpGet(std::string_view path, std::string_view host, uint16_t port,
                                       std::string_view activeRemote);

/// Sends one-shot DACP control requests to a sender. It resolves
/// `iTunes_Ctrl_<dacpId>` on `_dacp._tcp` through the injected resolver, then
/// connects and writes the request. Fire-and-forget: resolve or connect
/// failures are dropped. Lives on the reactor thread (the resolver's).
class DacpClient {
public:
    explicit DacpClient(platform::IServiceResolver& resolver);

    void send(std::string dacpId, std::string activeRemote, std::string path);

private:
    platform::IServiceResolver& m_resolver;
};

} // namespace tapein::core::session
