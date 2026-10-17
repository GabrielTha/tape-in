// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/util/result.h"

namespace tapein::core::platform {

/// Start-at-login control. macOS: SMAppService.mainAppService (13+).
/// Windows later: the HKCU Run key. Outside an app bundle (unit tests,
/// the CLI), the macOS backend reports unavailable.
class ILoginItem {
public:
    enum class Status { notRegistered, enabled, requiresApproval, notFound, unavailable };

    virtual ~ILoginItem() = default;
    virtual Result<void> setEnabled(bool enabled) = 0;
    [[nodiscard]] virtual Status status() const = 0;
    /// Opens the Login Items pane so the user can approve, where that applies.
    virtual void openSystemSettings() = 0;
};

} // namespace tapein::core::platform
