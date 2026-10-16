// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "fuzz_main.h"

#include "tapein/core/rtsp/rtsp_parser.h"

#include <span>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    using namespace tapein::core::rtsp;
    // Feed the whole input, then the same input one byte at a time: both
    // chunkings must behave (and never crash).
    Parser whole;
    (void)whole.feed(std::span<const uint8_t>(data, size));
    Parser chunked;
    for (size_t i = 0; i < size && i < 4096; ++i) {
        if (!chunked.feed(std::span<const uint8_t>(data + i, 1)).ok()) {
            break;
        }
    }
    return 0;
}
