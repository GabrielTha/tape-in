// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/platform/login_item.h"

namespace tapein::platform {

/// Start at login through SMAppService.mainAppService (macOS 13+). Only
/// meaningful for the registered .app bundle; from a plain executable
/// SMAppService reports an error, surfaced here as Status::unavailable.
class MacLoginItem final : public core::platform::ILoginItem {
public:
    core::Result<void> setEnabled(bool enabled) override;
    [[nodiscard]] Status status() const override;
    void openSystemSettings() override;
};

} // namespace tapein::platform
