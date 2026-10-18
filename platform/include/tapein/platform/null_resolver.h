// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/platform/service_resolver.h"

namespace tapein::platform {

/// Resolver with no backend: it replays a canned result. Used by tests, CI,
/// and on platforms without DNS-SD, where remote-control commands then become
/// no-ops. In immediate mode the callback fires inside resolve(); otherwise it
/// fires on the next processEvents().
class NullServiceResolver final : public core::platform::IServiceResolver {
public:
    explicit NullServiceResolver(core::Result<core::platform::ResolvedService> canned, bool immediate = true);

    void resolve(std::string instanceName, std::string type, Callback cb) override;
    [[nodiscard]] std::optional<core::net::NativeHandle> pollHandle() const override { return std::nullopt; }
    void processEvents() override;
    void cancel() override;

private:
    core::Result<core::platform::ResolvedService> m_canned;
    bool m_immediate;
    Callback m_pending;
};

} // namespace tapein::platform
