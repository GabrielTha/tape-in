// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/platform/service_advertiser.h"

#include <vector>

namespace tapein::platform {

/// Advertiser that publishes nothing. Used by tests, CI, and on platforms
/// without a DNS-SD backend yet. Records what it was asked to do.
class NullAdvertiser final : public core::platform::IServiceAdvertiser {
public:
    core::Result<void> publish(const core::platform::ServiceRecord& record) override;
    void unpublish() override;
    [[nodiscard]] std::optional<core::net::NativeHandle> pollHandle() const override { return std::nullopt; }
    void processEvents() override {}

    std::vector<core::platform::ServiceRecord> published;
    int unpublished = 0;
};

} // namespace tapein::platform
