// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/buffer/jitter_buffer.h"
#include "tapein/core/net/socket.h"
#include "tapein/core/session/dmap.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace tapein::core::session {

struct SessionInfo {
    uint64_t id = 0;
    std::string clientName; ///< X-Apple-Client-Name, may be empty
    std::string dacpId;
    std::string activeRemote;
    std::string userAgent;
    net::Endpoint peer;
    bool encrypted = false;
    bool pcm = false; ///< L16 instead of ALAC
};

struct SessionStats {
    uint64_t packetsReceived = 0;
    uint64_t resendRepliesReceived = 0;
    uint64_t packetsMissing = 0; ///< released as silence
    uint64_t resendRequestsSent = 0;
    uint64_t decodeErrors = 0;
    uint64_t packetsRejected = 0; ///< sequence behind the release point or too far ahead
    uint64_t resyncs = 0; ///< jitter buffer restarted after a sequence jump
    uint64_t datagramsFromStrangers = 0; ///< dropped: source address differs from the sender
    uint64_t framesDelivered = 0;
    uint64_t timingRepliesReceived = 0;
    uint32_t senderLatencyFrames = 0; ///< from the last sync packet
};

/// Receives decoded audio and session events. All calls happen on the
/// network thread, in order. onPcm blocks are 352 frames of interleaved
/// 16-bit stereo at 44.1 kHz.
class IPcmSink {
public:
    virtual ~IPcmSink() = default;
    virtual void onSessionStarted(const SessionInfo& info) = 0;
    virtual void onPcm(uint64_t sessionId, const buffer::ReleasedBlock& block) = 0;
    virtual void onVolume(uint64_t sessionId, float dB) = 0;
    /// The sender's buffering lead, from the latest sync packet. Optional.
    virtual void onSenderLatency(uint64_t /*sessionId*/, uint32_t /*frames*/) {}
    /// Now-playing pushed by the sender over SET_PARAMETER. All optional.
    virtual void onMetadata(uint64_t /*sessionId*/, const DmapMetadata& /*metadata*/) {}
    virtual void onArtwork(uint64_t /*sessionId*/, std::span<const uint8_t> /*image*/, std::string_view /*mime*/) {}
    virtual void onProgress(uint64_t /*sessionId*/, const DmapProgress& /*progress*/) {}
    virtual void onSessionEnded(uint64_t sessionId, std::string_view reason, const SessionStats& stats) = 0;
};

} // namespace tapein::core::session
