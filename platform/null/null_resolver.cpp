// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/platform/null_resolver.h"

namespace tapein::platform {

NullServiceResolver::NullServiceResolver(core::Result<core::platform::ResolvedService> canned, bool immediate)
    : m_canned(std::move(canned)), m_immediate(immediate)
{
}

void NullServiceResolver::resolve(std::string, std::string, Callback cb)
{
    if (m_immediate) {
        cb(m_canned);
    } else {
        m_pending = std::move(cb);
    }
}

void NullServiceResolver::processEvents()
{
    if (m_pending) {
        auto cb = std::move(m_pending);
        m_pending = nullptr;
        cb(m_canned);
    }
}

void NullServiceResolver::cancel() { m_pending = nullptr; }

} // namespace tapein::platform
