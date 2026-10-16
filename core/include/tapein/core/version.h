// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include <string_view>

namespace tapein::core {

/// Human-readable version of the core library, e.g. "0.1.0-phase1".
std::string_view versionString() noexcept;

} // namespace tapein::core
