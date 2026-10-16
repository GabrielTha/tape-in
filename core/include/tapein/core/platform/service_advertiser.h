// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/net/socket.h"
#include "tapein/core/util/result.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace tapein::core::platform {

/// A DNS-SD service to publish: `<instanceName>.<type>.local` on `port`
/// with the given TXT key/value pairs, in order.
struct ServiceRecord {
    std::string instanceName;
    std::string type;
    uint16_t port = 0;
    std::vector<std::pair<std::string, std::string>> txt;
};

/// Platform mDNS/DNS-SD registration. Implemented with dns_sd on macOS and
/// windns on Windows; a null implementation exists for tests and CI.
class IServiceAdvertiser {
public:
    virtual ~IServiceAdvertiser() = default;
    virtual Result<void> publish(const ServiceRecord& record) = 0;
    virtual void unpublish() = 0;
    /// A pollable handle the reactor should watch, if the backend has one.
    [[nodiscard]] virtual std::optional<net::NativeHandle> pollHandle() const = 0;
    /// Called by the reactor when pollHandle() is readable.
    virtual void processEvents() = 0;
};

} // namespace tapein::core::platform
