// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

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

} // namespace tapein::core::session
