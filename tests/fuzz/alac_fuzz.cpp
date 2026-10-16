// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "fuzz_main.h"

#include "tapein/core/codec/alac_decoder.h"

#include <array>
#include <span>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    using namespace tapein::core::codec;
    static const std::array<int32_t, 12> fmtp{96, 352, 0, 16, 40, 10, 14, 2, 255, 0, 0, 44100};
    static auto decoder = AlacDecoder::create(AlacConfig::fromFmtp(fmtp));
    static std::vector<int16_t> out(352 * 2);
    if (!decoder.ok()) {
        return 0;
    }
    (void)decoder.value().decode(std::span<const uint8_t>(data, size), out);
    return 0;
}
