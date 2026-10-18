// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/session/session.h"

#include <algorithm>
#include <cstring>

namespace tapein::core::session {

namespace {
constexpr int kInitialTimingRequests = 3;
constexpr std::chrono::milliseconds kTimingInitialInterval{300};
constexpr std::chrono::milliseconds kTimingSteadyInterval{3000};
} // namespace

Session::Session(net::Reactor& reactor, SessionInfo info, const rtsp::AnnounceParams& params,
                 std::optional<std::array<uint8_t, 16>> aesKey, IPcmSink& sink, SessionConfig config, Logger log)
    : m_reactor(reactor)
    , m_info(std::move(info))
    , m_params(params)
    , m_sink(sink)
    , m_config(config)
    , m_log(std::move(log))
    , m_jitter(static_cast<size_t>(params.framesPerPacket()))
{
    const size_t samples = static_cast<size_t>(params.framesPerPacket()) * buffer::kChannels;
    m_pcmScratch.assign(samples, 0);
    m_packetScratch.assign(rtp::kMaxDatagram, 0);
    if (aesKey && params.aesIv) {
        m_decryptor = std::make_unique<crypto::AesCbcDecryptor>(std::span<const uint8_t, 16>(aesKey->data(), 16),
                                                                std::span<const uint8_t, 16>(params.aesIv->data(), 16));
    }
    m_info.encrypted = m_decryptor != nullptr;
    m_info.pcm = params.isPcmL16;
    if (!params.isPcmL16) {
        auto decoder = codec::AlacDecoder::create(codec::AlacConfig::fromFmtp(params.fmtp));
        if (decoder.ok()) {
            m_decoder.emplace(std::move(decoder.value()));
        } else if (m_log) {
            m_log("ALAC decoder unavailable: " + decoder.error().message);
        }
    }
    m_lastActivityAt = std::chrono::steady_clock::now();
    m_setupTimer = m_reactor.addTimer(m_config.setupTimeout, [this] { onSetupTimeout(); }, false);
}

Session::~Session()
{
    end("destroyed");
}

Result<void> Session::setup(const net::Endpoint& localAddress, uint16_t clientControlPort, uint16_t clientTimingPort)
{
    if (m_setUp) {
        return makeError(ErrorCode::invalidState, "session already set up");
    }
    net::Endpoint bindTo = localAddress;
    bindTo.port = 0;

    auto audio = net::UdpSocket::bind(bindTo);
    auto control = net::UdpSocket::bind(bindTo);
    auto timing = net::UdpSocket::bind(bindTo);
    if (!audio.ok()) {
        return audio.error();
    }
    if (!control.ok()) {
        return control.error();
    }
    if (!timing.ok()) {
        return timing.error();
    }
    m_audioSocket = std::move(audio.value());
    m_controlSocket = std::move(control.value());
    m_timingSocket = std::move(timing.value());
    m_serverPort = m_audioSocket.localEndpoint().value().port;
    m_controlPort = m_controlSocket.localEndpoint().value().port;
    m_timingPort = m_timingSocket.localEndpoint().value().port;

    m_clientControl = m_info.peer;
    m_clientControl.port = clientControlPort;
    m_clientTiming = m_info.peer;
    m_clientTiming.port = clientTimingPort;

    m_reactor.add(m_audioSocket.handle(), [this] { onAudioReadable(); });
    m_reactor.add(m_controlSocket.handle(), [this] { onControlReadable(); });
    m_reactor.add(m_timingSocket.handle(), [this] { onTimingReadable(); });
    m_setUp = true;
    return {};
}

void Session::record()
{
    if (m_started || m_ended || !m_setUp) {
        return;
    }
    m_started = true;
    if (m_setupTimer != 0) {
        m_reactor.cancelTimer(m_setupTimer);
        m_setupTimer = 0;
    }
    m_lastActivityAt = std::chrono::steady_clock::now();
    m_sink.onSessionStarted(m_info);
    m_releaseTimer = m_reactor.addTimer(m_config.releaseInterval, [this] { onReleaseTick(); }, true);
    m_inactivityTimer = m_reactor.addTimer(std::chrono::seconds{1}, [this] { onInactivityTick(); }, true);
    sendTimingRequest();
}

void Session::flush(uint32_t rtpTimestamp)
{
    m_jitter.flushBefore(rtpTimestamp);
}

void Session::setVolume(float dB)
{
    m_volumeDb = dB;
    m_sink.onVolume(m_info.id, dB);
}

void Session::setMetadata(const DmapMetadata& metadata)
{
    m_sink.onMetadata(m_info.id, metadata);
}

void Session::setArtwork(std::span<const uint8_t> image, std::string_view mime)
{
    m_sink.onArtwork(m_info.id, image, mime);
}

void Session::setProgress(const DmapProgress& progress)
{
    m_sink.onProgress(m_info.id, progress);
}

void Session::end(std::string_view reason)
{
    if (m_ended) {
        return;
    }
    m_ended = true;
    if (m_releaseTimer != 0) {
        m_reactor.cancelTimer(m_releaseTimer);
    }
    if (m_timingTimer != 0) {
        m_reactor.cancelTimer(m_timingTimer);
    }
    if (m_inactivityTimer != 0) {
        m_reactor.cancelTimer(m_inactivityTimer);
    }
    if (m_setupTimer != 0) {
        m_reactor.cancelTimer(m_setupTimer);
    }
    if (m_setUp) {
        m_reactor.remove(m_audioSocket.handle());
        m_reactor.remove(m_controlSocket.handle());
        m_reactor.remove(m_timingSocket.handle());
        m_audioSocket.close();
        m_controlSocket.close();
        m_timingSocket.close();
    }
    if (m_started) {
        // Deliver whatever is still buffered, then tell the sink.
        m_jitter.release(0, std::chrono::steady_clock::now(), [this](const buffer::ReleasedBlock& b) { releaseBlock(b); });
        m_sink.onSessionEnded(m_info.id, reason, m_stats);
    }
    if (m_log) {
        m_log("session " + std::to_string(m_info.id) + " ended: " + std::string(reason));
    }
}

// ------------------------------------------------------------------ sockets

void Session::drain(net::UdpSocket& socket, uint8_t socketKind,
                    void (Session::*handler)(std::span<const uint8_t>, const net::Endpoint&))
{
    for (int i = 0; i < 64 && !m_ended; ++i) {
        auto r = socket.recvFrom(m_datagram);
        if (!r.ok() || r.value().first == 0) {
            return;
        }
        const std::span<const uint8_t> datagram(m_datagram.data(), r.value().first);
        // Only the sender we negotiated with may feed this session.
        const net::Endpoint& from = r.value().second;
        if (from.isV6 != m_info.peer.isV6 || from.addressBytes().size() != m_info.peer.addressBytes().size()
            || !std::equal(from.addressBytes().begin(), from.addressBytes().end(), m_info.peer.addressBytes().begin())) {
            ++m_stats.datagramsFromStrangers;
            continue;
        }
        if (m_config.captureRtp) {
            m_config.captureRtp(m_info.id, socketKind, datagram);
        }
        (this->*handler)(datagram, r.value().second);
    }
}

void Session::onAudioReadable()
{
    drain(m_audioSocket, 0, &Session::onAudioDatagram);
}

void Session::onControlReadable()
{
    drain(m_controlSocket, 1, &Session::onControlDatagram);
}

void Session::onTimingReadable()
{
    drain(m_timingSocket, 2, &Session::onTimingDatagram);
}

void Session::onAudioDatagram(std::span<const uint8_t> datagram, const net::Endpoint&)
{
    const auto packet = rtp::parseAudio(datagram);
    if (packet) {
        handleAudio(*packet);
    }
}

void Session::onControlDatagram(std::span<const uint8_t> datagram, const net::Endpoint&)
{
    const auto type = rtp::payloadType(datagram);
    if (!type) {
        return;
    }
    if (*type == rtp::PayloadType::sync) {
        if (const auto sync = rtp::parseSync(datagram)) {
            const uint32_t frames = sync->latencyFrames();
            if (frames != m_stats.senderLatencyFrames) {
                m_stats.senderLatencyFrames = frames;
                m_sink.onSenderLatency(m_info.id, frames);
            }
        }
    } else if (*type == rtp::PayloadType::resendReply) {
        if (const auto packet = rtp::parseAudio(datagram)) {
            handleAudio(*packet);
        }
    }
}

void Session::onTimingDatagram(std::span<const uint8_t> datagram, const net::Endpoint&)
{
    if (rtp::parseTimingReply(datagram)) {
        ++m_stats.timingRepliesReceived;
    }
}

void Session::handleAudio(const rtp::AudioPacket& packet)
{
    if (!m_started) {
        return;
    }
    m_lastActivityAt = std::chrono::steady_clock::now();
    ++m_stats.packetsReceived;
    if (packet.isResend) {
        ++m_stats.resendRepliesReceived;
    }
    if (packet.payload.size() > m_packetScratch.size()) {
        ++m_stats.decodeErrors;
        return;
    }

    std::span<const uint8_t> payload = packet.payload;
    if (m_decryptor) {
        std::memcpy(m_packetScratch.data(), payload.data(), payload.size());
        std::span<uint8_t> scratch(m_packetScratch.data(), payload.size());
        if (!m_decryptor->decryptPacketInPlace(scratch)) {
            ++m_stats.decodeErrors;
            return;
        }
        payload = scratch;
    }

    const size_t framesPerPacket = m_jitter.framesPerPacket();
    std::fill(m_pcmScratch.begin(), m_pcmScratch.end(), int16_t{0});

    if (m_params.isPcmL16) {
        const size_t frames = std::min(payload.size() / 4, framesPerPacket);
        if (frames != framesPerPacket && !m_warnedFrameCount && m_log) {
            m_warnedFrameCount = true;
            m_log("L16 packet with " + std::to_string(payload.size() / 4) + " frames; expected "
                  + std::to_string(framesPerPacket));
        }
        for (size_t i = 0; i < frames * 2; ++i) {
            m_pcmScratch[i] = static_cast<int16_t>(static_cast<uint16_t>((payload[i * 2] << 8) | payload[i * 2 + 1]));
        }
    } else {
        if (!m_decoder) {
            ++m_stats.decodeErrors;
            return;
        }
        auto decoded = m_decoder->decode(payload, m_pcmScratch);
        if (!decoded.ok()) {
            ++m_stats.decodeErrors;
            return;
        }
    }

    if (m_jitter.insert(packet.seq, packet.rtpTimestamp, m_pcmScratch, m_lastActivityAt)) {
        m_consecutiveRejections = 0;
        return;
    }
    ++m_stats.packetsRejected;
    if (++m_consecutiveRejections >= m_config.resyncAfterRejections) {
        // The sender's sequence is nowhere near ours (restart, or a stray
        // first packet set the release point): start over from this packet.
        m_consecutiveRejections = 0;
        ++m_stats.resyncs;
        m_jitter.release(0, m_lastActivityAt, [this](const buffer::ReleasedBlock& b) { releaseBlock(b); });
        m_jitter.reset();
        (void)m_jitter.insert(packet.seq, packet.rtpTimestamp, m_pcmScratch, m_lastActivityAt);
        if (m_log) {
            m_log("session " + std::to_string(m_info.id) + " resynchronised at sequence " + std::to_string(packet.seq));
        }
    }
}

// ------------------------------------------------------------------- timers

void Session::onReleaseTick()
{
    const auto now = std::chrono::steady_clock::now();
    m_jitter.release(m_config.holdBackPackets, now, [this](const buffer::ReleasedBlock& b) { releaseBlock(b); });

    const auto ranges = m_jitter.dueResendRequests(now, m_config.resendFirstAfter, m_config.resendRepeatEvery);
    for (const auto& range : ranges) {
        const auto request = rtp::buildResendRequest(m_resendRequestSeq++, range.first, range.count);
        if (m_controlSocket.sendTo(request, m_clientControl).ok()) {
            ++m_stats.resendRequestsSent;
        }
    }
}

void Session::releaseBlock(const buffer::ReleasedBlock& block)
{
    if (block.wasMissing) {
        ++m_stats.packetsMissing;
    }
    m_stats.framesDelivered += block.pcm.size() / buffer::kChannels;
    m_sink.onPcm(m_info.id, block);
}

void Session::sendTimingRequest()
{
    if (m_ended) {
        return;
    }
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const uint64_t unixNs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
    const auto request = rtp::buildTimingRequest(rtp::ntpFromNanoseconds(unixNs));
    (void)m_timingSocket.sendTo(request, m_clientTiming);
    ++m_timingRequestsSent;
    const auto next = m_timingRequestsSent < kInitialTimingRequests ? kTimingInitialInterval : kTimingSteadyInterval;
    m_timingTimer = m_reactor.addTimer(next, [this] { sendTimingRequest(); }, false);
}

void Session::onInactivityTick()
{
    if (!m_started || m_ended) {
        return;
    }
    if (std::chrono::steady_clock::now() - m_lastActivityAt > m_config.inactivityTimeout) {
        endSelf("inactive for " + std::to_string(m_config.inactivityTimeout.count() / 1000) + " s");
    }
}

void Session::onSetupTimeout()
{
    m_setupTimer = 0;
    if (m_started || m_ended) {
        return;
    }
    endSelf("no RECORD within " + std::to_string(m_config.setupTimeout.count() / 1000) + " s");
}

void Session::endSelf(std::string_view reason)
{
    end(reason);
    if (m_config.onSelfEnded) {
        m_config.onSelfEnded(m_info.id);
    }
}

} // namespace tapein::core::session
