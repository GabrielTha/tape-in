// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/session/engine.h"

#include "tapein/core/util/text.h"

#include <algorithm>
#include <cmath>
#include <future>
#include <utility>

namespace tapein::core::session {

namespace {
constexpr uint32_t kFramesPerPacket = 352;
constexpr uint32_t kMinTargetFrames = 8820; ///< 200 ms floor for sender-sync
} // namespace

float raopVolumeDbToGain(float dB) noexcept
{
    if (dB <= -144.0f) {
        return 0.0f;
    }
    return std::pow(10.0f, dB / 20.0f);
}


Engine::Engine(EngineConfig config, platform::IServiceAdvertiser& advertiser, std::shared_ptr<crypto::RaopRsaKey> key,
               net::Reactor* externalReactor, ResolverFactory resolverFactory)
    : m_config(std::move(config)), m_advertiser(advertiser), m_key(std::move(key)),
      m_ownsReactor(externalReactor == nullptr),
      m_mixer(static_cast<uint32_t>(std::max<size_t>(1, m_config.receiver.maxSessions)), dsp::SourceStream::kDefaultMaxBlock)
{
    if (m_ownsReactor) {
        m_ownReactor = std::make_unique<net::Reactor>();
        m_reactor = m_ownReactor.get();
    } else {
        m_reactor = externalReactor;
    }
    if (resolverFactory) {
        m_resolver = resolverFactory(*m_reactor);
        if (m_resolver) {
            m_dacp = std::make_unique<DacpClient>(*m_resolver);
        }
    }
    m_name = util::clampUtf8(m_config.receiver.name, 63);
    if (m_name.empty()) {
        m_name = "Tape In";
    }
    m_config.receiver.name = m_name;
    m_config.receiver.holdBackPackets = holdBackPacketsFor(m_config.bufferMode);
}

Engine::~Engine()
{
    stop();
}

uint16_t Engine::holdBackPacketsFor(BufferMode mode) const noexcept
{
    switch (mode) {
    case BufferMode::shortBuffer:
        return 31;
    case BufferMode::monitoring:
        return 8;
    case BufferMode::senderSync:
    default:
        return 63;
    }
}

uint32_t Engine::targetFramesFor(BufferMode mode, uint32_t senderLatencyFrames) const noexcept
{
    switch (mode) {
    case BufferMode::shortBuffer:
        return 11025; // 250 ms
    case BufferMode::monitoring:
        return 4410; // 100 ms
    case BufferMode::senderSync:
    default: {
        const uint32_t holdBack = holdBackPacketsFor(mode) * kFramesPerPacket;
        const uint32_t lead = senderLatencyFrames > holdBack ? senderLatencyFrames - holdBack : 0;
        return std::max(kMinTargetFrames, lead);
    }
    }
}

void Engine::applyTarget(Source& src)
{
    if (src.stream != nullptr) {
        src.stream->params().targetFillFrames.store(targetFramesFor(m_config.bufferMode, src.senderLatencyFrames),
                                                     std::memory_order_relaxed);
    }
}

Result<void> Engine::start()
{
    if (m_running) {
        return Result<void>{};
    }
    rebuildReceiver();
    if (!m_lastError.empty()) {
        return makeError(ErrorCode::ioError, m_lastError);
    }
    runOnReactor([this] {
        // Live session stats are copied from the receiver on the reactor
        // thread (the only thread that mutates its session list) into the
        // Engine's own, mutex-protected Source::stats, which sources() reads.
        m_statsTimer = m_reactor->addTimer(std::chrono::milliseconds{250}, [this] { refreshStats(); }, true);
    });
    m_running = true;
    if (m_ownsReactor) {
        m_thread = std::thread([this] { m_reactor->run(); });
    }
    return Result<void>{};
}

void Engine::stop()
{
    if (!m_running) {
        return;
    }
    runOnReactor([this] {
        if (m_statsTimer != 0) {
            m_reactor->cancelTimer(m_statsTimer);
            m_statsTimer = 0;
        }
        std::unique_ptr<Receiver> old;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            old = std::move(m_receiver);
        }
        if (old) {
            old->stop();
            old.reset();
        }
    });
    m_running = false;
    if (m_ownsReactor && m_thread.joinable()) {
        m_reactor->stop();
        m_thread.join();
    }
    // Drop every source; the audio thread is expected to be stopped by now.
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [dacp, src] : m_sources) {
        if (src->slot != UINT32_MAX) {
            m_mixer.remove(src->slot);
        }
    }
    m_mixer.collectGarbage();
    m_sources.clear();
    m_bySession.clear();
}

void Engine::runOnReactor(std::function<void()> fn)
{
    if (!m_ownsReactor || !m_thread.joinable()) {
        fn(); // the caller is (or stands in for) the reactor thread
        return;
    }
    std::promise<void> done;
    auto future = done.get_future();
    m_reactor->post([&fn, &done] {
        fn();
        done.set_value();
    });
    future.wait();
}

void Engine::rebuildReceiver()
{
    runOnReactor([this] {
        // Move the old receiver out under the lock so a concurrent reader
        // (rtspPort) never sees a half-reset pointer, then tear it down with
        // the lock released: stop() ends sessions, which re-enters
        // onSessionEnded and takes the same mutex.
        std::unique_ptr<Receiver> old;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            old = std::move(m_receiver);
            m_config.receiver.name = m_name;
            m_config.receiver.holdBackPackets = holdBackPacketsFor(m_config.bufferMode);
        }
        if (old) {
            old->stop();
            old.reset();
        }
        auto receiver = std::make_unique<Receiver>(*m_reactor, m_config.receiver, *this, m_advertiser, m_key);
        auto r = receiver->start();
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!r.ok()) {
            m_lastError = r.error().message;
            emit({EngineEvent::Kind::receiverError, 0, m_lastError});
            return;
        }
        m_lastError.clear();
        m_receiver = std::move(receiver);
        emit({EngineEvent::Kind::receiverStarted, 0, m_name});
    });
}

void Engine::setName(std::string name)
{
    std::string clamped = util::clampUtf8(name, 63);
    if (clamped.empty()) {
        clamped = "Tape In";
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (clamped == m_name) {
            return;
        }
        m_name = clamped;
    }
    rebuildReceiver();
}

void Engine::setBufferMode(BufferMode mode)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config.bufferMode = mode;
    m_config.receiver.holdBackPackets = holdBackPacketsFor(mode);
    for (auto& [dacp, src] : m_sources) {
        applyTarget(*src);
    }
}

void Engine::setIgnoreSenderVolume(bool ignore)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config.ignoreSenderVolume = ignore;
    for (auto& [dacp, src] : m_sources) {
        if (src->stream != nullptr) {
            src->stream->params().ignoreSenderGain.store(ignore, std::memory_order_relaxed);
        }
    }
}

void Engine::setRequirePassword(bool require, std::string password)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_config.receiver.requirePassword == require && m_config.receiver.password == password) {
            return;
        }
        m_config.receiver.requirePassword = require;
        m_config.receiver.password = std::move(password);
    }
    rebuildReceiver();
}

void Engine::setExclusiveSolo(bool exclusive)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config.exclusiveSolo = exclusive;
}

void Engine::setAskBeforeAccept(bool ask)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config.askBeforeAccept = ask;
}

void Engine::approveConnection(uint64_t sourceId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (Source* src = byId(sourceId); src != nullptr && src->pending) {
        src->pending = false;
        if (src->stream != nullptr) {
            src->stream->params().held.store(false, std::memory_order_relaxed);
        }
        emit({EngineEvent::Kind::sourceChanged, sourceId, {}});
    }
}

void Engine::declineConnection(uint64_t sourceId)
{
    uint64_t sessionId = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        Source* src = byId(sourceId);
        if (src == nullptr || !src->pending) {
            return;
        }
        sessionId = src->sessionId; // stays pending so onSessionEnded removes it at once
    }
    // End the RTSP session off-lock on the reactor: endSession re-enters
    // onSessionEnded, which takes m_mutex and, seeing the source still pending,
    // removes it immediately.
    runOnReactor([this, sessionId] {
        if (m_receiver) {
            m_receiver->dropSession(sessionId, "declined");
        }
    });
}

void Engine::startRecording()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_recordMix.clear();
    m_recordMix.reserve(static_cast<size_t>(kMaxRecordSeconds) * 44100u * 2u);
    // Sources already live at the downbeat start together at offset 0, so their
    // blocks mix-add on a shared timeline; a source that joins later is aligned
    // to the mix's length on its first block (onPcm).
    for (auto& [dacp, src] : m_sources) {
        src->recordPos = 0;
        src->recordJoined = src->live;
    }
    m_recording = true;
}

RecordedSample Engine::stopRecording()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    RecordedSample out;
    if (!m_recording) {
        return out; // nothing was being captured
    }
    m_recording = false;
    out.interleaved.resize(m_recordMix.size());
    for (size_t i = 0; i < m_recordMix.size(); ++i) {
        out.interleaved[i] = static_cast<int16_t>(std::clamp(m_recordMix[i], -32768, 32767));
    }
    m_recordMix.clear();
    m_recordMix.shrink_to_fit();
    // Name the capture after whatever live source carries now-playing metadata.
    for (auto& [dacp, src] : m_sources) {
        if (src->live && !src->metadata.trackName.empty()) {
            out.trackName = src->metadata.trackName;
            out.artist = src->metadata.artist;
            out.album = src->metadata.album;
            break;
        }
    }
    return out;
}

RecordStatus Engine::recordStatus() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    RecordStatus st;
    st.recording = m_recording;
    st.frames = static_cast<uint32_t>(m_recordMix.size() / 2);
    return st;
}

std::vector<float> Engine::inputWaveform() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<float> out(kWaveformBins);
    // Oldest first; the bin after the one currently filling is the oldest.
    for (size_t i = 0; i < kWaveformBins; ++i) {
        out[i] = m_waveform[(m_waveformHead + 1 + i) % kWaveformBins];
    }
    return out;
}

void Engine::setUserGainDb(uint64_t sourceId, float dB)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (Source* src = byId(sourceId)) {
        src->userGainDb = dB;
        if (src->stream != nullptr) {
            src->stream->params().userGain.store(raopVolumeDbToGain(dB), std::memory_order_relaxed);
        }
    }
}

void Engine::setMute(uint64_t sourceId, bool mute)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (Source* src = byId(sourceId)) {
        src->mute = mute;
        if (src->stream != nullptr) {
            src->stream->params().mute.store(mute, std::memory_order_relaxed);
        }
    }
}

void Engine::setSolo(uint64_t sourceId, bool solo)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    Source* src = byId(sourceId);
    if (src == nullptr) {
        return;
    }
    src->solo = solo;
    if (src->stream != nullptr) {
        src->stream->params().solo.store(solo, std::memory_order_relaxed);
    }
    // Exclusive solo: enabling one clears every other source.
    if (solo && m_config.exclusiveSolo) {
        for (auto& [dacp, other] : m_sources) {
            if (other.get() == src) {
                continue;
            }
            other->solo = false;
            if (other->stream != nullptr) {
                other->stream->params().solo.store(false, std::memory_order_relaxed);
            }
        }
    }
}

void Engine::sendTransportCommand(uint64_t sourceId, TransportCommand cmd)
{
    runOnReactor([this, sourceId, cmd] {
        std::string dacpId;
        std::string activeRemote;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            Source* src = byId(sourceId);
            // Remote control stays available while the source is known and
            // addressable, even when not live: a paused sender tears the audio
            // session down but keeps its DACP control channel, so play must
            // still reach it to resume.
            if (src == nullptr || src->info.activeRemote.empty() || !m_dacp) {
                return;
            }
            dacpId = src->info.dacpId;
            activeRemote = src->info.activeRemote;
        }
        m_dacp->send(dacpId, activeRemote, dacpPath(cmd));
    });
}

void Engine::setSenderVolumeDb(uint64_t sourceId, float dB)
{
    runOnReactor([this, sourceId, dB] {
        std::string dacpId;
        std::string activeRemote;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            Source* src = byId(sourceId);
            if (src == nullptr || !src->live || src->info.activeRemote.empty() || !m_dacp) {
                return;
            }
            dacpId = src->info.dacpId;
            activeRemote = src->info.activeRemote;
        }
        m_dacp->send(dacpId, activeRemote, dacpVolumePath(dB));
    });
}

Engine::Source* Engine::bySession(uint64_t sessionId) noexcept
{
    const auto it = m_bySession.find(sessionId);
    return it == m_bySession.end() ? nullptr : it->second;
}

Engine::Source* Engine::byId(uint64_t sourceId) noexcept
{
    for (auto& [dacp, src] : m_sources) {
        if (src->sourceId == sourceId) {
            return src.get();
        }
    }
    return nullptr;
}

void Engine::emit(EngineEvent event)
{
    m_events.push_back(std::move(event));
}

void Engine::onSessionStarted(const SessionInfo& info)
{
    auto ring = buffer::HeapRing::create(m_config.ringFrames);
    if (!ring.ok()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        emit({EngineEvent::Kind::receiverError, 0, "cannot allocate ring: " + ring.error().message});
        return;
    }
    auto streamResult = dsp::SourceStream::create(std::move(ring.value()), m_config.quality);
    if (!streamResult.ok()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        emit({EngineEvent::Kind::receiverError, 0, "cannot create stream: " + streamResult.error().message});
        return;
    }
    auto stream = std::move(streamResult.value());

    std::lock_guard<std::mutex> lock(m_mutex);
    const std::string key = info.dacpId.empty() ? ("peer:" + info.peer.toString()) : info.dacpId;
    auto it = m_sources.find(key);
    bool replacement = false;
    bool created = false;
    Source* src = nullptr;
    if (it != m_sources.end()) {
        src = it->second.get();
        if (src->lingerTimer != 0) {
            m_reactor->cancelTimer(src->lingerTimer);
            src->lingerTimer = 0;
        }
        if (src->slot != UINT32_MAX) {
            // The source already holds a mixer slot — either a still-live
            // stream being replaced, or a lingering (already silent) one
            // reconnecting. Retire that slot either way, or it leaks: a live
            // stream fades first, a lingering one goes at once.
            const uint32_t oldSlot = src->slot;
            if (src->live && src->stream != nullptr) {
                src->stream->params().stopRequested.store(true, std::memory_order_relaxed);
                m_reactor->addTimer(std::chrono::milliseconds{150}, [this, oldSlot] {
                    m_mixer.remove(oldSlot);
                    scheduleGc();
                }, false);
            } else {
                m_mixer.remove(oldSlot);
                scheduleGc();
            }
            src->slot = UINT32_MAX;
            src->stream = nullptr;
            replacement = true;
        }
    } else {
        auto owned = std::make_unique<Source>();
        owned->sourceId = m_nextSourceId++;
        owned->dacpId = info.dacpId;
        src = owned.get();
        m_sources.emplace(key, std::move(owned));
        created = true;
    }

    stream->params().userGain.store(raopVolumeDbToGain(src->userGainDb), std::memory_order_relaxed);
    stream->params().mute.store(src->mute, std::memory_order_relaxed);
    stream->params().solo.store(src->solo, std::memory_order_relaxed);
    stream->params().senderGain.store(raopVolumeDbToGain(src->senderVolumeDb), std::memory_order_relaxed);
    stream->params().ignoreSenderGain.store(m_config.ignoreSenderVolume, std::memory_order_relaxed);

    dsp::SourceStream* streamPtr = stream.get();
    auto slot = m_mixer.add(std::move(stream));
    if (!slot.has_value()) {
        emit({EngineEvent::Kind::receiverError, 0, "mixer full; cannot add source"});
        if (created) {
            m_sources.erase(key); // do not leave a ghost source with no slot
        }
        return;
    }
    src->sessionId = info.id;
    src->slot = *slot;
    src->stream = streamPtr;
    src->live = true;
    src->info = info;
    src->senderLatencyFrames = 0;
    // Ask-before-accept: accept the RTSP session but hold the source silent
    // (and flag it for the UI) until the user approves it. Re-armed on every
    // (re)connection, since decisions are not remembered.
    src->pending = m_config.askBeforeAccept;
    streamPtr->params().held.store(src->pending, std::memory_order_relaxed);
    applyTarget(*src);
    m_bySession[info.id] = src;
    emit({replacement ? EngineEvent::Kind::sourceChanged : EngineEvent::Kind::sourceAdded, src->sourceId, info.clientName});
}

void Engine::onPcm(uint64_t sessionId, const buffer::ReleasedBlock& block)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    Source* src = bySession(sessionId);
    if (src == nullptr || src->stream == nullptr) {
        return;
    }
    src->stream->ring().write(block.pcm);

    // Live input preview: one peak bin per block, the max across all sources
    // that land in the same slot, so the waveform tracks whatever is streaming.
    int16_t blockPeak = 0;
    for (int16_t s : block.pcm) {
        blockPeak = std::max<int16_t>(blockPeak, static_cast<int16_t>(std::abs(s)));
    }
    const float peak = static_cast<float>(blockPeak) / 32768.0f;
    // Bins cover fixed 8 ms of wall time, so the preview scrolls steadily no
    // matter how bursty the packets arrive or how many sources feed it.
    constexpr auto kBinDuration = std::chrono::milliseconds(8);
    const auto now = std::chrono::steady_clock::now();
    if (m_waveformTime == std::chrono::steady_clock::time_point{}
        || now - m_waveformTime > kBinDuration * kWaveformBins) {
        m_waveform.fill(0.0f); // first block, or a gap longer than the window
        m_waveformTime = now;
    }
    while (now - m_waveformTime >= kBinDuration) {
        m_waveformTime += kBinDuration;
        m_waveformHead = (m_waveformHead + 1) % kWaveformBins;
        m_waveform[m_waveformHead] = 0.0f;
    }
    float& bin = m_waveform[m_waveformHead];
    bin = std::max(bin, peak);

    if (!m_recording) {
        return;
    }
    // Reactor thread, under m_mutex: mix-add this source's clean PCM into the
    // shared accumulator at its own cursor (capacity reserved on start).
    if (!src->recordJoined) {
        src->recordPos = m_recordMix.size();
        src->recordJoined = true;
    }
    const size_t cap = static_cast<size_t>(kMaxRecordSeconds) * 44100u * 2u;
    if (src->recordPos + block.pcm.size() > cap) {
        m_recording = false; // hit the safety cap
        return;
    }
    if (src->recordPos + block.pcm.size() > m_recordMix.size()) {
        m_recordMix.resize(src->recordPos + block.pcm.size(), 0);
    }
    for (size_t i = 0; i < block.pcm.size(); ++i) {
        m_recordMix[src->recordPos + i] += block.pcm[i];
    }
    src->recordPos += block.pcm.size();
}

void Engine::onVolume(uint64_t sessionId, float dB)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    Source* src = bySession(sessionId);
    if (src != nullptr) {
        src->senderVolumeDb = dB;
        if (src->stream != nullptr) {
            src->stream->params().senderGain.store(raopVolumeDbToGain(dB), std::memory_order_relaxed);
        }
    }
}

void Engine::onSenderLatency(uint64_t sessionId, uint32_t frames)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    Source* src = bySession(sessionId);
    if (src != nullptr) {
        src->senderLatencyFrames = frames;
        applyTarget(*src);
    }
}

void Engine::onMetadata(uint64_t sessionId, const DmapMetadata& metadata)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    Source* src = bySession(sessionId);
    if (src != nullptr) {
        src->metadata = metadata;
        emit({EngineEvent::Kind::sourceChanged, src->sourceId, {}});
    }
}

void Engine::onArtwork(uint64_t sessionId, std::span<const uint8_t> image, std::string_view mime)
{
    constexpr size_t kMaxArtworkBytes = 1024 * 1024;
    std::lock_guard<std::mutex> lock(m_mutex);
    Source* src = bySession(sessionId);
    if (src != nullptr && image.size() <= kMaxArtworkBytes) {
        src->artwork.assign(image.begin(), image.end());
        src->artworkMime = std::string(mime);
        emit({EngineEvent::Kind::sourceChanged, src->sourceId, {}});
    }
}

void Engine::onProgress(uint64_t sessionId, const DmapProgress& progress)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    Source* src = bySession(sessionId);
    if (src != nullptr) {
        src->progress = progress;
        emit({EngineEvent::Kind::sourceChanged, src->sourceId, {}});
    }
}

void Engine::onSessionEnded(uint64_t sessionId, std::string_view /*reason*/, const SessionStats& stats)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    Source* src = bySession(sessionId);
    if (src == nullptr) {
        return;
    }
    m_bySession.erase(sessionId);
    // A newer session may already have taken over this source (replacement).
    if (src->sessionId != sessionId) {
        return;
    }
    src->live = false;
    src->stats = stats;
    const uint64_t sourceId = src->sourceId;
    const std::string key = src->dacpId.empty() ? ("peer:" + src->info.peer.toString()) : src->dacpId;
    // A connection that ends while still pending (declined, or abandoned before
    // the user chose) was never approved, so it vanishes at once rather than
    // lingering as a "disconnected" row.
    if (src->pending) {
        if (src->slot != UINT32_MAX) {
            m_mixer.remove(src->slot);
            scheduleGc();
        }
        m_sources.erase(key);
        emit({EngineEvent::Kind::sourceRemoved, sourceId, {}});
        return;
    }
    emit({EngineEvent::Kind::sourceChanged, sourceId, "disconnected"});
    src->lingerTimer = m_reactor->addTimer(m_config.lingerTime, [this, key, sourceId] {
        std::lock_guard<std::mutex> lk(m_mutex);
        auto it = m_sources.find(key);
        if (it == m_sources.end() || it->second->sourceId != sourceId || it->second->live) {
            return; // reconnected in the meantime
        }
        if (it->second->slot != UINT32_MAX) {
            m_mixer.remove(it->second->slot);
        }
        m_sources.erase(it);
        emit({EngineEvent::Kind::sourceRemoved, sourceId, {}});
        scheduleGc();
    }, false);
}

void Engine::scheduleGc()
{
    if (m_gcScheduled) {
        return;
    }
    m_gcScheduled = true;
    m_reactor->addTimer(std::chrono::seconds{1}, [this] {
        m_gcScheduled = false;
        m_mixer.collectGarbage();
    }, false);
}

void Engine::setAudioActive(bool active)
{
    m_mixer.setAudioActive(active);
}

void Engine::refreshStats()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_receiver) {
        return;
    }
    for (auto& [dacp, src] : m_sources) {
        if (src->live) {
            src->stats = m_receiver->sessionStats(src->sessionId);
        }
    }
}

std::vector<SourceSnapshot> Engine::sources() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<SourceSnapshot> out;
    out.reserve(m_sources.size());
    for (const auto& [dacp, src] : m_sources) {
        SourceSnapshot s;
        s.sourceId = src->sourceId;
        s.pending = src->pending;
        s.clientName = src->info.clientName;
        s.dacpId = src->info.dacpId;
        s.activeRemote = src->info.activeRemote;
        s.peer = src->info.peer.toString();
        s.userAgent = src->info.userAgent;
        s.live = src->live;
        s.userGainDb = src->userGainDb;
        s.mute = src->mute;
        s.solo = src->solo;
        s.senderVolumeDb = src->senderVolumeDb;
        s.targetFillFrames = targetFramesFor(m_config.bufferMode, src->senderLatencyFrames);
        s.stats = src->stats;
        s.trackName = src->metadata.trackName;
        s.artist = src->metadata.artist;
        s.album = src->metadata.album;
        s.hasArtwork = !src->artwork.empty();
        s.artwork = src->artwork;
        s.artworkMime = src->artworkMime;
        constexpr uint32_t kSampleRate = 44100;
        const auto& pg = src->progress;
        s.elapsedSeconds = pg.currentFrames >= pg.startFrames ? (pg.currentFrames - pg.startFrames) / kSampleRate : 0u;
        s.durationSeconds = pg.endFrames >= pg.startFrames ? (pg.endFrames - pg.startFrames) / kSampleRate : 0u;
        if (src->stream != nullptr) {
            const auto& m = src->stream->meters();
            s.state = static_cast<dsp::StreamState>(m.state.load(std::memory_order_relaxed));
            s.peakL = m.peakL.load(std::memory_order_relaxed);
            s.peakR = m.peakR.load(std::memory_order_relaxed);
            s.rms = m.rms.load(std::memory_order_relaxed);
            s.fillFrames = m.fillFrames.load(std::memory_order_relaxed);
            s.underruns = m.underruns.load(std::memory_order_relaxed);
            s.ratioDeviation = m.ratioDeviation.load(std::memory_order_relaxed);
        }
        out.push_back(std::move(s));
    }
    std::sort(out.begin(), out.end(), [](const SourceSnapshot& a, const SourceSnapshot& b) { return a.sourceId < b.sourceId; });
    return out;
}

std::vector<EngineEvent> Engine::pollEvents()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::exchange(m_events, {});
}

std::string Engine::name() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_name;
}

uint16_t Engine::rtspPort() const
{
    // The reactor thread swaps m_receiver on rebuild/stop; the lock keeps this
    // read off a pointer mid-reassignment. rtspPort() itself returns a value
    // fixed at the receiver's start, so reading it under the lock is safe.
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_receiver ? m_receiver->rtspPort() : 0;
}

std::string Engine::lastError() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastError;
}

} // namespace tapein::core::session
