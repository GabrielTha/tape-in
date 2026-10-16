// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/platform/null_advertiser.h"

namespace tapein::platform {

core::Result<void> NullAdvertiser::publish(const core::platform::ServiceRecord& record)
{
    published.push_back(record);
    return {};
}

void NullAdvertiser::unpublish()
{
    ++unpublished;
}

} // namespace tapein::platform
