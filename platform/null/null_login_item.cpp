// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/platform/null_login_item.h"

namespace tapein::platform {

core::Result<void> NullLoginItem::setEnabled(bool enabled)
{
    m_enabled = enabled;
    return core::Result<void>{};
}

core::platform::ILoginItem::Status NullLoginItem::status() const
{
    return m_enabled ? Status::enabled : Status::notRegistered;
}

} // namespace tapein::platform
