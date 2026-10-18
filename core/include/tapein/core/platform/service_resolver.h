// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/net/socket.h"
#include "tapein/core/util/result.h"

#include <functional>
#include <optional>
#include <string>

namespace tapein::core::platform {

/// The host and address a DNS-SD instance resolved to.
struct ResolvedService {
    std::string instanceName;
    std::string hostTarget; ///< the SRV target, e.g. "Gabriels-iPhone.local."
    net::Endpoint endpoint; ///< address + port to connect to
};

/// Resolves one DNS-SD instance (`<instanceName>.<type>.local`) to an address
/// and port. Used to reach a sender's DACP control server
/// (`iTunes_Ctrl_<DACP-ID>` on `_dacp._tcp`). Like IServiceAdvertiser, a
/// backend with a pollable fd exposes it via pollHandle()/processEvents() so
/// the reactor can drive it; a synchronous backend returns nullopt.
class IServiceResolver {
public:
    virtual ~IServiceResolver() = default;
    using Callback = std::function<void(Result<ResolvedService>)>;

    /// Starts a resolve; `cb` is invoked exactly once with the result (on the
    /// reactor thread for async backends). A new resolve supersedes any
    /// in-flight one.
    virtual void resolve(std::string instanceName, std::string type, Callback cb) = 0;
    [[nodiscard]] virtual std::optional<net::NativeHandle> pollHandle() const = 0;
    virtual void processEvents() = 0;
    virtual void cancel() = 0;
};

} // namespace tapein::core::platform
