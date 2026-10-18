// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "fuzz_main.h"

#include "tapein/core/session/dmap.h"

#include <span>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    using namespace tapein::core::session;
    // The whole input is a hostile DMAP metadata body.
    (void)parseDmapMetadata(std::span<const uint8_t>(data, size));
    // The same bytes, read as a progress line, must also never crash.
    (void)parseProgress(std::string_view(reinterpret_cast<const char*>(data), size));
    return 0;
}
