// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/platform/login_item.h"

namespace tapein::platform {

/// Remembers the flag in memory. Used by tests, the CLI, and platforms
/// without a backend yet.
class NullLoginItem final : public core::platform::ILoginItem {
public:
    core::Result<void> setEnabled(bool enabled) override;
    [[nodiscard]] Status status() const override;
    void openSystemSettings() override {}

private:
    bool m_enabled = false;
};

} // namespace tapein::platform
