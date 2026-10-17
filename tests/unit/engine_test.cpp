// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors
//
// Drives an Engine over loopback with the scripted RAOP client and pulls
// audio through its mixer, the way the JUCE audio callback will.

#include "tapein/core/crypto/raop_key.h"
#include "tapein/core/net/reactor.h"
#include "tapein/core/session/engine.h"
#include "tapein/platform/null_advertiser.h"
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
                     std::chrono::milliseconds linger = 300ms)
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
        engine = std::make_unique<Engine>(std::move(cfg), advertiser, key, &reactor);
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
