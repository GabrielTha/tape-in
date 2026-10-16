// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "fuzz_main.h"

#include "tapein/core/buffer/jitter_buffer.h"
#include "tapein/core/rtp/rtp_packets.h"

#include <chrono>
#include <span>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    using namespace tapein::core;
    const std::span<const uint8_t> datagram(data, size);
    (void)rtp::payloadType(datagram);
    (void)rtp::parseSync(datagram);
    (void)rtp::parseTimingReply(datagram);
    if (const auto audio = rtp::parseAudio(datagram)) {
        // Exercise the jitter buffer with the parsed header too.
        static buffer::JitterBuffer jitter;
        static std::vector<int16_t> pcm(buffer::kFramesPerSlot * buffer::kChannels, 0);
        const auto now = std::chrono::steady_clock::now();
        (void)jitter.insert(audio->seq, audio->rtpTimestamp, pcm, now);
        jitter.release(2, now, [](const buffer::ReleasedBlock&) {});
        (void)jitter.dueResendRequests(now, std::chrono::milliseconds{0}, std::chrono::milliseconds{0});
    }
    return 0;
}
