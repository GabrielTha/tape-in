// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "fuzz_main.h"

#include "tapein/core/rtsp/sdp.h"

#include <span>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    (void)tapein::core::rtsp::parseAnnounceSdp(std::span<const uint8_t>(data, size));
    return 0;
}
