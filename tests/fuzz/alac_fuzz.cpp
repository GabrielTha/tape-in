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
    static std::vector<int16_t> out(352 * 2);
    // The first 4 bytes vary the sender-controlled cookie fields (pb, mb,
    // kb, max run); the rest is the packet. Frames/depth/channels stay at
    // the only values v1 accepts.
    if (size < 4) {
        return 0;
    }
    AlacConfig config = AlacConfig::fromFmtp({96, 352, 0, 16, 40, 10, 14, 2, 255, 0, 0, 44100});
    config.pb = data[0];
    config.mb = data[1];
    config.kb = data[2];
    config.maxRun = static_cast<uint16_t>(data[3] * 2);
    auto decoder = AlacDecoder::create(config);
    if (!decoder.ok()) {
        return 0;
    }
    (void)decoder.value().decode(std::span<const uint8_t>(data + 4, size - 4), out);
    return 0;
}
