// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors
//
// Drives an Engine over loopback with the scripted RAOP client and pulls
// audio through its mixer, the way the JUCE audio callback will.

#include "tapein/core/crypto/raop_key.h"
#include "tapein/core/net/reactor.h"
#include "tapein/core/net/socket.h"
#include "tapein/core/session/engine.h"
#include "tapein/platform/null_advertiser.h"
#include "tapein/platform/null_resolver.h"
#include "support/raop_client.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace tapein::core;
using namespace tapein::core::session;
using namespace tapein::test;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

namespace {

struct Fixture {
    net::Reactor reactor;
    tapein::platform::NullAdvertiser advertiser;
    std::shared_ptr<crypto::RaopRsaKey> key = crypto::RaopRsaKey::load().value();
    std::unique_ptr<Engine> engine;

    explicit Fixture(BufferMode mode = BufferMode::monitoring, uint16_t holdBack = 2,
                     std::chrono::milliseconds linger = 300ms, Engine::ResolverFactory factory = nullptr)
    {
        EngineConfig cfg;
        cfg.receiver.name = "Tape In Test";
        cfg.receiver.hardwareId = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
        cfg.receiver.holdBackPackets = holdBack;
        cfg.receiver.bindAddress = loopback(0);
        cfg.bufferMode = mode;
        cfg.lingerTime = linger;
        cfg.ringFrames = 1u << 16;
        cfg.quality = dsp::ResamplerQuality::fastest;
        engine = std::make_unique<Engine>(std::move(cfg), advertiser, key, &reactor, std::move(factory));
        REQUIRE(engine->start().ok());
        REQUIRE(engine->rtspPort() != 0);
    }

    RaopClient client()
    {
        return RaopClient(engine->rtspPort(), [this](std::chrono::milliseconds ms) { reactor.runOnce(ms); });
    }

    void pump(std::chrono::milliseconds d)
    {
        const auto until = Clock::now() + d;
        while (Clock::now() < until) {
            reactor.runOnce(5ms);
        }
    }

    template <class F>
    bool pumpUntil(F&& cond, std::chrono::milliseconds timeout = 3000ms)
    {
        const auto until = Clock::now() + timeout;
        while (Clock::now() < until) {
            if (cond()) {
                return true;
            }
            reactor.runOnce(5ms);
        }
        return cond();
    }

    // Renders one stereo block and returns the peak absolute sample.
    float renderPeak(uint32_t frames = 512, double rate = 44100.0)
    {
        std::vector<float> l(frames, 0.0f), r(frames, 0.0f);
        float* outs[2] = {l.data(), r.data()};
        engine->mixer().render(outs, 2, frames, rate);
        float peak = 0.0f;
        for (uint32_t i = 0; i < frames; ++i) {
            peak = std::max({peak, std::abs(l[i]), std::abs(r[i])});
        }
        return peak;
    }

    float renderPeakOver(int blocks)
    {
        float peak = 0.0f;
        for (int i = 0; i < blocks; ++i) {
            peak = std::max(peak, renderPeak());
        }
        return peak;
    }
};

// Feeds `packets` of a loud L16 tone starting at sequence/timestamp 0, pumping between sends.
void feedTone(Fixture& fx, RaopClient& c, int packets)
{
    for (int i = 0; i < packets; ++i) {
        std::vector<int16_t> pcm(kSamples);
        for (size_t s = 0; s < kFrames; ++s) {
            const auto v = static_cast<int16_t>(std::lround(0.5 * 32767.0 * std::sin(2.0 * 3.141592653589793 * 1000.0 * static_cast<double>(static_cast<size_t>(i) * kFrames + s) / 44100.0)));
            pcm[s * 2] = v;
            pcm[s * 2 + 1] = v;
        }
        c.sendAudio(static_cast<uint16_t>(i), static_cast<uint32_t>(i) * kFrames, l16Payload(pcm));
        fx.reactor.runOnce(2ms);
    }
}

void sendSyncLatency(RaopClient& c, uint32_t latencyFrames)
{
    std::vector<uint8_t> sync{0x90, 0xD4, 0x00, 0x07, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                              static_cast<uint8_t>(latencyFrames >> 24), static_cast<uint8_t>(latencyFrames >> 16),
                              static_cast<uint8_t>(latencyFrames >> 8), static_cast<uint8_t>(latencyFrames)};
    REQUIRE(c.audio.sendTo(sync, loopback(c.serverControlPort)).ok());
}

SourceSnapshot onlySource(Fixture& fx)
{
    auto v = fx.engine->sources();
    REQUIRE(v.size() == 1);
    return v.front();
}

} // namespace

TEST_CASE("a session becomes a source with the client name and its audio reaches the mixer")
{
    Fixture fx;
    auto c = fx.client();
    REQUIRE(c.startStream(true).status == 200);
    REQUIRE(fx.pumpUntil([&] { return !fx.engine->sources().empty(); }));
    auto snap = onlySource(fx);
    CHECK(snap.clientName == "Test iPhone");
    CHECK(snap.live);

    feedTone(fx, c, 30);
    fx.pump(100ms);
    CHECK(fx.renderPeakOver(8) > 0.3f);
    CHECK(static_cast<dsp::StreamState>(onlySource(fx).state) == dsp::StreamState::playing);
}

TEST_CASE("RTSP volume sets the sender gain unless ignoreSenderVolume is on")
{
    Fixture fx;
    auto c = fx.client();
    REQUIRE(c.startStream(true).status == 200);
    REQUIRE(fx.pumpUntil([&] { return !fx.engine->sources().empty(); }));
    feedTone(fx, c, 30);
    fx.pump(50ms);
    REQUIRE(c.request("SET_PARAMETER", "DACP-ID: " + c.dacpId + "\r\nContent-Type: text/parameters\r\n", "volume: -20.0\r\n").status == 200);
    REQUIRE(fx.pumpUntil([&] { return std::abs(onlySource(fx).senderVolumeDb + 20.0f) < 0.01f; }));
    const float quiet = fx.renderPeakOver(8);
    CHECK(quiet > 0.02f);
    CHECK(quiet < 0.1f);

    fx.engine->setIgnoreSenderVolume(true);
    const float loud = fx.renderPeakOver(8);
    CHECK(loud > 0.4f);
}

TEST_CASE("now-playing metadata and progress reach the source snapshot")
{
    Fixture fx;
    auto c = fx.client();
    REQUIRE(c.startStream(true).status == 200);
    REQUIRE(fx.pumpUntil([&] { return !fx.engine->sources().empty(); }));

    // DMAP metadata: minm + asar.
    std::string dmap;
    auto appendTag = [&](const char* code, std::string_view v) {
        dmap += code;
        const auto n = static_cast<uint32_t>(v.size());
        dmap.push_back(static_cast<char>((n >> 24) & 0xFF));
        dmap.push_back(static_cast<char>((n >> 16) & 0xFF));
        dmap.push_back(static_cast<char>((n >> 8) & 0xFF));
        dmap.push_back(static_cast<char>(n & 0xFF));
        dmap += v;
    };
    appendTag("minm", "Midnight C");
    appendTag("asar", "M83");

    REQUIRE(c.request("SET_PARAMETER", "DACP-ID: " + c.dacpId + "\r\nContent-Type: application/x-dmap-tagged\r\n", dmap).status == 200);
    // progress: start=1, cur=44101 (1 s in), end=176401 (~4 s).
    REQUIRE(c.request("SET_PARAMETER", "DACP-ID: " + c.dacpId + "\r\nContent-Type: text/parameters\r\n", "progress: 1/44101/176401\r\n").status == 200);

    REQUIRE(fx.pumpUntil([&] { return onlySource(fx).trackName == "Midnight C"; }));
    auto snap = onlySource(fx);
    CHECK(snap.artist == "M83");
    CHECK(snap.elapsedSeconds == 1u);
    CHECK(snap.durationSeconds >= 3u);

    bool sawChanged = false;
    for (const auto& e : fx.engine->pollEvents()) {
        if (e.kind == EngineEvent::Kind::sourceChanged) {
            sawChanged = true;
        }
    }
    CHECK(sawChanged);
}

TEST_CASE("sendTransportCommand resolves and sends a DACP GET with the source's Active-Remote")
{
    using tapein::core::platform::ResolvedService;
    using tapein::platform::NullServiceResolver;

    // A capturing server stands in for the sender's DACP control port.
    auto listener = net::TcpListener::bind(loopback(0)).value();
    const uint16_t dacpPort = listener.localEndpoint().value().port;
    ResolvedService svc{"iTunes_Ctrl_X", "host.local.", loopback(dacpPort)};

    Fixture fx(BufferMode::monitoring, 2, 300ms, [svc](net::Reactor&) -> std::unique_ptr<tapein::core::platform::IServiceResolver> {
        return std::make_unique<NullServiceResolver>(Result<ResolvedService>(svc));
    });

    std::string captured;
    std::optional<net::TcpSocket> accepted;
    fx.reactor.add(listener.handle(), [&] {
        auto s = listener.accept();
        if (s.ok()) {
            accepted.emplace(std::move(s.value()));
            fx.reactor.add(accepted->handle(), [&] {
                std::array<uint8_t, 512> buf{};
                auto n = accepted->read(buf);
                if (n.ok() && n.value() > 0)
                    captured.append(reinterpret_cast<const char*>(buf.data()), n.value());
            });
        }
    });

    auto c = fx.client();
    REQUIRE(c.startStream(true).status == 200);
    REQUIRE(fx.pumpUntil([&] { return !fx.engine->sources().empty(); }));
    const auto id = onlySource(fx).sourceId;

    fx.engine->sendTransportCommand(id, TransportCommand::next);
    REQUIRE(fx.pumpUntil([&] { return !captured.empty(); }));
    CHECK(captured.find("GET /ctrl-int/1/nextitem ") != std::string::npos);
    CHECK(captured.find("Active-Remote: 424242") != std::string::npos);

    fx.reactor.remove(listener.handle());
}

TEST_CASE("transport still reaches a paused source via its Active-Remote, but not an unknown one")
{
    using tapein::core::platform::ResolvedService;
    using tapein::platform::NullServiceResolver;

    auto listener = net::TcpListener::bind(loopback(0)).value();
    const uint16_t dacpPort = listener.localEndpoint().value().port;
    ResolvedService svc{"iTunes_Ctrl_X", "host.local.", loopback(dacpPort)};

    Fixture fx(BufferMode::monitoring, 2, 2000ms, [svc](net::Reactor&) -> std::unique_ptr<tapein::core::platform::IServiceResolver> {
        return std::make_unique<NullServiceResolver>(Result<ResolvedService>(svc));
    });

    std::string captured;
    std::optional<net::TcpSocket> accepted;
    fx.reactor.add(listener.handle(), [&] {
        auto s = listener.accept();
        if (s.ok()) {
            accepted.emplace(std::move(s.value()));
            fx.reactor.add(accepted->handle(), [&] {
                std::array<uint8_t, 512> buf{};
                auto n = accepted->read(buf);
                if (n.ok() && n.value() > 0)
                    captured.append(reinterpret_cast<const char*>(buf.data()), n.value());
            });
        }
    });

    auto c = fx.client();
    REQUIRE(c.startStream(true).status == 200);
    REQUIRE(fx.pumpUntil([&] { return !fx.engine->sources().empty(); }));
    const auto id = onlySource(fx).sourceId;

    // The sender pauses: it tears the RAOP audio session down, so the source
    // goes not-live and lingers, but its DACP control channel stays reachable.
    REQUIRE(c.request("TEARDOWN", "Session: 1\r\n").status == 200);
    REQUIRE(fx.pumpUntil([&] { return !onlySource(fx).live; })); // lingering, not live

    // Play must still be forwarded so the sender can resume playback.
    fx.engine->sendTransportCommand(id, TransportCommand::playPause);
    REQUIRE(fx.pumpUntil([&] { return !captured.empty(); }));
    CHECK(captured.find("GET /ctrl-int/1/playpause ") != std::string::npos);

    // An unknown source id addresses no one.
    captured.clear();
    fx.engine->sendTransportCommand(id + 9999, TransportCommand::playPause);
    fx.pump(100ms);
    CHECK(captured.empty());

    fx.reactor.remove(listener.handle());
}

TEST_CASE("teardown leaves the source lingering, then removes it with an event")
{
    Fixture fx(BufferMode::monitoring, 2, 150ms);
    auto c = fx.client();
    REQUIRE(c.startStream(true).status == 200);
    REQUIRE(fx.pumpUntil([&] { return !fx.engine->sources().empty(); }));
    const uint64_t id = onlySource(fx).sourceId;
    REQUIRE(c.request("TEARDOWN", "DACP-ID: " + c.dacpId + "\r\n").status == 200);
    REQUIRE(fx.pumpUntil([&] { return !onlySource(fx).live; }));
    CHECK(fx.engine->sources().size() == 1); // still lingering
    REQUIRE(fx.pumpUntil([&] { return fx.engine->sources().empty(); }, 2000ms));
    auto events = fx.engine->pollEvents();
    CHECK(std::any_of(events.begin(), events.end(),
                      [id](const EngineEvent& e) { return e.kind == EngineEvent::Kind::sourceRemoved && e.sourceId == id; }));
}

TEST_CASE("the same DACP-ID reconnecting keeps user gain, mute and solo")
{
    Fixture fx(BufferMode::monitoring, 2, 5000ms);
    auto c = fx.client();
    REQUIRE(c.startStream(true).status == 200);
    REQUIRE(fx.pumpUntil([&] { return !fx.engine->sources().empty(); }));
    const uint64_t id = onlySource(fx).sourceId;
    fx.engine->setUserGainDb(id, -6.0f);
    fx.engine->setMute(id, true);
    fx.engine->setSolo(id, true);
    REQUIRE(c.request("TEARDOWN", "DACP-ID: " + c.dacpId + "\r\n").status == 200);
    REQUIRE(fx.pumpUntil([&] { return !onlySource(fx).live; }));

    auto c2 = fx.client();
    c2.dacpId = c.dacpId; // same client
    REQUIRE(c2.startStream(true).status == 200);
    REQUIRE(fx.pumpUntil([&] { return onlySource(fx).live; }));
    auto snap = onlySource(fx);
    CHECK(snap.sourceId == id);
    CHECK(std::abs(snap.userGainDb + 6.0f) < 0.01f);
    CHECK(snap.mute);
    CHECK(snap.solo);
}

TEST_CASE("setName restarts the receiver; empty falls back; a long UTF-8 name is cut to 63 bytes on a boundary")
{
    Fixture fx;
    fx.engine->setName("Studio Mac");
    REQUIRE(fx.pumpUntil([&] {
        return !fx.advertiser.published.empty() && fx.advertiser.published.back().instanceName.find("@Studio Mac") != std::string::npos;
    }));
    CHECK(fx.engine->name() == "Studio Mac");

    fx.engine->setName("");
    REQUIRE(fx.pumpUntil([&] { return fx.engine->name() == "Tape In"; }));

    const std::string accents(100, '\0');
    std::string longName;
    for (int i = 0; i < 100; ++i) {
        longName += "\xC3\xA9"; // 'é', two bytes each → 200 bytes
    }
    fx.engine->setName(longName);
    REQUIRE(fx.pumpUntil([&] { return fx.engine->name() != "Tape In" && fx.engine->name() != ""; }));
    const std::string& n = fx.engine->name();
    CHECK(n.size() <= 63);
    CHECK(n.size() % 2 == 0); // never split a 2-byte codepoint
}

TEST_CASE("a sync latency below the hold-back clamps the ring target to 8820 frames")
{
    Fixture fx(BufferMode::senderSync, 2);
    auto c = fx.client();
    REQUIRE(c.startStream(true).status == 200);
    REQUIRE(fx.pumpUntil([&] { return !fx.engine->sources().empty(); }));
    sendSyncLatency(c, 1000); // below holdBack(2)*352 = 704 margin → clamp
    REQUIRE(fx.pumpUntil([&] { return onlySource(fx).targetFillFrames == 8820; }));
}

TEST_CASE("the buffer mode sets the ring target for new sessions")
{
    {
        Fixture fx(BufferMode::shortBuffer, 2);
        auto c = fx.client();
        REQUIRE(c.startStream(true).status == 200);
        REQUIRE(fx.pumpUntil([&] { return !fx.engine->sources().empty(); }));
        CHECK(onlySource(fx).targetFillFrames == 11025);
    }
    {
        Fixture fx(BufferMode::monitoring, 2);
        auto c = fx.client();
        REQUIRE(c.startStream(true).status == 200);
        REQUIRE(fx.pumpUntil([&] { return !fx.engine->sources().empty(); }));
        CHECK(onlySource(fx).targetFillFrames == 4410);
    }
}

TEST_CASE("stop with a live source frees it only after audio is inactive")
{
    Fixture fx;
    auto c = fx.client();
    REQUIRE(c.startStream(true).status == 200);
    REQUIRE(fx.pumpUntil([&] { return !fx.engine->sources().empty(); }));
    feedTone(fx, c, 20);
    fx.pump(50ms);
    fx.engine->setAudioActive(true);
    fx.renderPeakOver(2);
    fx.engine->setAudioActive(false);
    fx.engine->stop(); // must not hang or crash
    CHECK(fx.engine->sources().empty());
}

TEST_CASE("every render block carries audio once playing: no periodic silent blocks")
{
    Fixture fx(BufferMode::monitoring, 2);
    auto c = fx.client();
    REQUIRE(c.startStream(true).status == 200);
    REQUIRE(fx.pumpUntil([&] { return !fx.engine->sources().empty(); }));
    fx.engine->setAudioActive(true);

    // Faithful real-time model: the reactor (releasing audio into the ring on
    // its 10 ms timer) and the audio callback (consuming 512 frames per block
    // at 48 kHz = 10.67 ms) advance by the same wall-clock time. Feed packets
    // at the sender's 44.1 kHz pace. A block that renders silence once playing,
    // with input available, is a real dropout.
    int silentBlocks = 0;
    int audioBlocks = 0;
    int seq = 0;
    double owedInputFrames = 0.0; // 44.1 kHz frames we still owe the sender pace
    for (int block = 0; block < 300; ++block) {
        // One 512-frame output block at 48 kHz spans 10.667 ms, i.e. 470.4
        // input frames at 44.1 kHz.
        owedInputFrames += 44100.0 * 512.0 / 48000.0;
        while (owedInputFrames >= kFrames) {
            std::vector<int16_t> pcm(kSamples);
            for (size_t sm = 0; sm < kFrames; ++sm) {
                const auto v = static_cast<int16_t>(std::lround(0.5 * 32767.0 * std::sin(2.0 * 3.141592653589793 * 1000.0 * static_cast<double>(static_cast<size_t>(seq) * kFrames + sm) / 44100.0)));
                pcm[sm * 2] = v;
                pcm[sm * 2 + 1] = v;
            }
            c.sendAudio(static_cast<uint16_t>(seq), static_cast<uint32_t>(seq) * kFrames, l16Payload(pcm));
            ++seq;
            owedInputFrames -= kFrames;
        }
        // Advance the reactor by one block's worth of wall-clock time so its
        // release timer fires in step with consumption.
        const auto until = Clock::now() + std::chrono::microseconds(10667);
        while (Clock::now() < until) {
            fx.reactor.runOnce(1ms);
        }
        const float peak = fx.renderPeak(512, 48000.0);
        if (block > 80) { // after buffering + warm-up
            if (peak < 0.05f) {
                ++silentBlocks;
            } else {
                ++audioBlocks;
            }
        }
    }
    INFO("audio blocks " << audioBlocks << " silent blocks " << silentBlocks);
    CHECK(audioBlocks > 150);
    CHECK(silentBlocks == 0);
}

TEST_CASE("reconnect within the linger window does not leak a mixer slot")
{
    Fixture fx(BufferMode::monitoring, 2, 5000ms);
    auto countOccupied = [&] {
        size_t n = 0;
        for (uint32_t i = 0; i < fx.engine->mixer().maxSources(); ++i) {
            if (fx.engine->mixer().source(i) != nullptr) {
                ++n;
            }
        }
        return n;
    };

    auto c = fx.client();
    REQUIRE(c.startStream(true).status == 200);
    REQUIRE(fx.pumpUntil([&] { return !fx.engine->sources().empty(); }));
    CHECK(countOccupied() == 1);

    REQUIRE(c.request("TEARDOWN", "DACP-ID: " + c.dacpId + "\r\n").status == 200);
    REQUIRE(fx.pumpUntil([&] { return !onlySource(fx).live; }));
    CHECK(countOccupied() == 1); // lingering, still holds its slot

    auto c2 = fx.client();
    c2.dacpId = c.dacpId; // same client reconnects while the old one lingers
    REQUIRE(c2.startStream(true).status == 200);
    REQUIRE(fx.pumpUntil([&] { return onlySource(fx).live; }));
    fx.pump(300ms); // let the delayed slot removal run
    // Exactly one stream must occupy the mixer: the old one was retired.
    CHECK(countOccupied() == 1);
}

TEST_CASE("owned reactor: concurrent reads with churn are race-free")
{
    // The shipping configuration: the Engine owns its network thread, which
    // the external-reactor fixtures never exercise. Reader threads touch
    // Engine state the reactor thread mutates. Meant to run under TSan.
    tapein::platform::NullAdvertiser advertiser;
    auto key = crypto::RaopRsaKey::load().value();

    // Phase 1: sources()/rtspPort() hammered while sessions churn (issue 1).
    {
        EngineConfig cfg;
        cfg.receiver.name = "Churn";
        cfg.receiver.bindAddress = loopback(0);
        cfg.bufferMode = BufferMode::monitoring;
        cfg.lingerTime = 40ms;
        Engine engine(std::move(cfg), advertiser, key);
        REQUIRE(engine.start().ok());
        REQUIRE(engine.rtspPort() != 0);

        std::atomic<bool> stop{false};
        std::thread reader([&] {
            while (!stop.load()) {
                (void)engine.sources();
                (void)engine.rtspPort();
                (void)engine.pollEvents();
            }
        });
        for (int round = 0; round < 6; ++round) {
            RaopClient c(engine.rtspPort(), [](std::chrono::milliseconds d) { std::this_thread::sleep_for(d); });
            if (c.startStream(true).status == 200) {
                for (int pk = 0; pk < 15; ++pk) {
                    c.sendAudio(static_cast<uint16_t>(pk), static_cast<uint32_t>(pk) * kFrames,
                                l16Payload(tone(static_cast<uint16_t>(pk))));
                    std::this_thread::sleep_for(2ms);
                }
                (void)c.request("TEARDOWN", "DACP-ID: " + c.dacpId + "\r\n");
            }
            std::this_thread::sleep_for(15ms);
        }
        stop.store(true);
        reader.join();
        engine.stop();
    }

    // Phase 2: rtspPort()/name() hammered while the receiver is rebuilt by
    // setName (issue 2); no clients, so no connection races the restart.
    {
        EngineConfig cfg;
        cfg.receiver.name = "Rename";
        cfg.receiver.bindAddress = loopback(0);
        Engine engine(std::move(cfg), advertiser, key);
        REQUIRE(engine.start().ok());

        std::atomic<bool> stop{false};
        std::thread reader([&] {
            while (!stop.load()) {
                (void)engine.rtspPort();
                (void)engine.name();
                (void)engine.lastError();
            }
        });
        for (int i = 0; i < 20; ++i) {
            engine.setName("Name " + std::to_string(i));
            std::this_thread::sleep_for(5ms);
        }
        stop.store(true);
        reader.join();
        engine.stop();
    }
    SUCCEED("no crash or sanitizer report");
}

namespace {
std::vector<uint64_t> sourceIds(Fixture& fx)
{
    std::vector<uint64_t> ids;
    for (const auto& s : fx.engine->sources()) {
        ids.push_back(s.sourceId);
    }
    return ids;
}
bool soloOf(Fixture& fx, uint64_t id)
{
    for (const auto& s : fx.engine->sources()) {
        if (s.sourceId == id) {
            return s.solo;
        }
    }
    return false;
}
} // namespace

TEST_CASE("exclusive solo clears solo on the other sources")
{
    Fixture fx;
    fx.engine->setExclusiveSolo(true);
    auto a = fx.client();
    REQUIRE(a.startStream(true).status == 200);
    REQUIRE(fx.pumpUntil([&] { return fx.engine->sources().size() == 1; }));
    auto b = fx.client();
    REQUIRE(b.startStream(true).status == 200);
    REQUIRE(fx.pumpUntil([&] { return fx.engine->sources().size() == 2; }));

    const auto ids = sourceIds(fx);
    REQUIRE(ids.size() == 2);
    const uint64_t idA = ids[0], idB = ids[1];

    fx.engine->setSolo(idA, true);
    CHECK(soloOf(fx, idA));
    fx.engine->setSolo(idB, true); // exclusive → clears A
    CHECK_FALSE(soloOf(fx, idA));
    CHECK(soloOf(fx, idB));
}

TEST_CASE("non-exclusive solo lets several sources solo at once")
{
    Fixture fx; // exclusiveSolo defaults false
    auto a = fx.client();
    REQUIRE(a.startStream(true).status == 200);
    REQUIRE(fx.pumpUntil([&] { return fx.engine->sources().size() == 1; }));
    auto b = fx.client();
    REQUIRE(b.startStream(true).status == 200);
    REQUIRE(fx.pumpUntil([&] { return fx.engine->sources().size() == 2; }));

    const auto ids = sourceIds(fx);
    REQUIRE(ids.size() == 2);
    fx.engine->setSolo(ids[0], true);
    fx.engine->setSolo(ids[1], true);
    CHECK(soloOf(fx, ids[0]));
    CHECK(soloOf(fx, ids[1]));
}

TEST_CASE("setRequirePassword re-advertises pw=true")
{
    Fixture fx;
    fx.engine->setRequirePassword(true, "hunter2");
    REQUIRE(fx.pumpUntil([&] {
        if (fx.advertiser.published.empty()) {
            return false;
        }
        const auto& rec = fx.advertiser.published.back();
        for (const auto& [k, v] : rec.txt) {
            if (k == "pw") {
                return v == "true";
            }
        }
        return false;
    }));
    // An empty password disables it again.
    fx.engine->setRequirePassword(true, "");
    REQUIRE(fx.pumpUntil([&] {
        const auto& rec = fx.advertiser.published.back();
        for (const auto& [k, v] : rec.txt) {
            if (k == "pw") {
                return v == "false";
            }
        }
        return false;
    }));
}

TEST_CASE("ask-before-accept holds a new connection silent until it is approved")
{
    Fixture fx;
    fx.engine->setAskBeforeAccept(true);
    auto c = fx.client();
    REQUIRE(c.startStream(true).status == 200);
    REQUIRE(fx.pumpUntil([&] { return !fx.engine->sources().empty(); }));
    auto snap = onlySource(fx);
    CHECK(snap.pending); // the UI shows Allow/Decline
    CHECK(snap.live);    // the RTSP session was accepted

    // While pending the source is held silent, even with audio flowing.
    feedTone(fx, c, 30);
    fx.pump(100ms);
    CHECK(fx.renderPeakOver(8) < 0.01f);

    // Approving clears pending and lets the audio through.
    fx.engine->approveConnection(snap.sourceId);
    CHECK_FALSE(onlySource(fx).pending);
    feedTone(fx, c, 30);
    fx.pump(100ms);
    CHECK(fx.renderPeakOver(8) > 0.3f);
}

TEST_CASE("declining a pending connection ends the session and removes the source at once")
{
    // A long linger proves the decline removes immediately rather than waiting it out.
    Fixture fx(BufferMode::monitoring, 2, 5000ms);
    fx.engine->setAskBeforeAccept(true);
    auto c = fx.client();
    REQUIRE(c.startStream(true).status == 200);
    REQUIRE(fx.pumpUntil([&] { return !fx.engine->sources().empty(); }));
    const uint64_t id = onlySource(fx).sourceId;
    REQUIRE(onlySource(fx).pending);

    fx.engine->declineConnection(id);
    REQUIRE(fx.pumpUntil([&] { return fx.engine->sources().empty(); }, 2000ms));
    auto events = fx.engine->pollEvents();
    CHECK(std::any_of(events.begin(), events.end(),
                      [id](const EngineEvent& e) { return e.kind == EngineEvent::Kind::sourceRemoved && e.sourceId == id; }));
}

TEST_CASE("global recording captures the clean input between start and stop")
{
    Fixture fx;
    auto c = fx.client();
    REQUIRE(c.startStream(true).status == 200);
    REQUIRE(fx.pumpUntil([&] { return !fx.engine->sources().empty(); }));
    CHECK_FALSE(fx.engine->recordStatus().recording);

    fx.engine->startRecording();
    CHECK(fx.engine->recordStatus().recording);
    feedTone(fx, c, 30);
    fx.pump(80ms);
    CHECK(fx.engine->recordStatus().frames >= 10u * kFrames);
    auto sample = fx.engine->stopRecording();

    CHECK_FALSE(fx.engine->recordStatus().recording);
    CHECK(sample.channels == 2);
    CHECK(sample.sampleRate == 44100);
    CHECK(sample.interleaved.size() >= 10u * kFrames * 2u); // most of the fed tone
    int16_t peak = 0;
    for (int16_t s : sample.interleaved) {
        peak = std::max<int16_t>(peak, static_cast<int16_t>(std::abs(s)));
    }
    CHECK(peak > 10000); // the loud tone, captured clean
}

TEST_CASE("stopRecording without an active recording yields nothing")
{
    Fixture fx;
    auto sample = fx.engine->stopRecording();
    CHECK(sample.interleaved.empty());
    CHECK_FALSE(fx.engine->recordStatus().recording);
}

TEST_CASE("the input waveform preview fills with the live signal envelope")
{
    Fixture fx;
    auto c = fx.client();
    REQUIRE(c.startStream(true).status == 200);
    REQUIRE(fx.pumpUntil([&] { return !fx.engine->sources().empty(); }));
    feedTone(fx, c, 60);
    fx.pump(60ms);

    // The preview follows playback: render the mixer as the audio device would.
    std::vector<std::vector<float>> ch(2, std::vector<float>(512));
    float* ptrs[2] = {ch[0].data(), ch[1].data()};
    for (int i = 0; i < 35; ++i) {
        fx.engine->mixer().render(ptrs, 2, 512, 44100.0);
    }

    auto wave = fx.engine->inputWaveform();
    CHECK_FALSE(wave.empty());
    float peak = 0.0f;
    for (float v : wave) {
        peak = std::max(peak, v);
    }
    CHECK(peak > 0.3f); // the loud tone shows in the preview envelope
}


TEST_CASE("a nameless sender is named from its resolved DACP host")
{
    using tapein::core::platform::ResolvedService;
    using tapein::platform::NullServiceResolver;

    ResolvedService svc{"iTunes_Ctrl_X", "Gabriels-iPhone.local.", loopback(1)};
    Fixture fx(BufferMode::monitoring, 2, 300ms,
               [svc](net::Reactor&) -> std::unique_ptr<tapein::core::platform::IServiceResolver> {
                   return std::make_unique<NullServiceResolver>(Result<ResolvedService>(svc));
               });

    auto c = fx.client();
    c.clientName.clear(); // this sender never says who it is
    REQUIRE(c.startStream(true).status == 200);
    REQUIRE(fx.pumpUntil([&] { return !fx.engine->sources().empty(); }));
    REQUIRE(fx.pumpUntil([&] { return onlySource(fx).clientName == "Gabriels iPhone"; }, 1000ms));
}

TEST_CASE("the record waveform scrolls a fixed window as the capture advances")
{
    Fixture fx;
    auto c = fx.client();
    REQUIRE(c.startStream(true).status == 200);
    REQUIRE(fx.pumpUntil([&] { return !fx.engine->sources().empty(); }));

    fx.engine->startRecording();
    const auto fresh = fx.engine->recordWaveform();
    REQUIRE(fresh.size() == kWaveformBins); // the window is full-size from the start
    CHECK(*std::max_element(fresh.begin(), fresh.end()) == 0.0f);

    feedTone(fx, c, 30);
    fx.pump(60ms);
    const auto during = fx.engine->recordWaveform();
    REQUIRE(during.size() == kWaveformBins);
    CHECK(*std::max_element(during.begin(), during.end()) > 0.3f); // the tone shows while recording

    // Feed enough silence that the loud bins scroll out of the window: the
    // view slides along the take instead of compressing the whole of it.
    for (int i = 30; i < 30 + 320; ++i) {
        std::vector<int16_t> pcm(kSamples, 0);
        c.sendAudio(static_cast<uint16_t>(i), static_cast<uint32_t>(i) * kFrames, l16Payload(pcm));
        fx.reactor.runOnce(2ms);
    }
    fx.pump(60ms);
    const auto later = fx.engine->recordWaveform();
    REQUIRE(later.size() == kWaveformBins);
    CHECK(*std::max_element(later.begin(), later.end()) < 0.05f); // the tone scrolled out

    (void)fx.engine->stopRecording();
    CHECK(fx.engine->recordWaveform().empty()); // cleared once the take ends
}
