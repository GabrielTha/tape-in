// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors
//
// Drives a Receiver over loopback with a scripted RTSP/RTP client. The test
// thread runs the reactor between steps, so everything is deterministic.

#include "tapein/core/buffer/jitter_buffer.h"
#include "tapein/core/crypto/apple_challenge.h"
#include "tapein/core/crypto/raop_key.h"
#include "tapein/core/net/reactor.h"
#include "tapein/core/net/socket.h"
#include "tapein/core/rtsp/rtsp_parser.h"
#include "tapein/core/session/dmap.h"
#include "tapein/core/session/pcm_sink.h"
#include "tapein/core/session/receiver.h"
#include "tapein/core/util/base64.h"
#include "tapein/platform/null_advertiser.h"
#include "support/raop_client.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <thread>
#include <vector>

using namespace tapein::core;
using namespace tapein::core::session;
using namespace tapein::test;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

namespace {

struct Block {
    uint16_t seq;
    bool wasMissing;
    std::vector<int16_t> pcm;
};

struct RecordingSink : IPcmSink {
    std::vector<SessionInfo> started;
    std::map<uint64_t, std::vector<Block>> blocks;
    std::map<uint64_t, float> volume;
    std::vector<std::pair<uint64_t, std::string>> ended;

    void onSessionStarted(const SessionInfo& info) override { started.push_back(info); }
    void onPcm(uint64_t id, const buffer::ReleasedBlock& b) override
    {
        blocks[id].push_back(Block{b.seq, b.wasMissing, std::vector<int16_t>(b.pcm.begin(), b.pcm.end())});
    }
    void onVolume(uint64_t id, float dB) override { volume[id] = dB; }
    std::map<uint64_t, DmapMetadata> metadata;
    std::map<uint64_t, std::pair<std::vector<uint8_t>, std::string>> artwork;
    std::map<uint64_t, DmapProgress> progress;
    void onMetadata(uint64_t id, const DmapMetadata& md) override { metadata[id] = md; }
    void onArtwork(uint64_t id, std::span<const uint8_t> img, std::string_view mime) override
    {
        artwork[id] = {std::vector<uint8_t>(img.begin(), img.end()), std::string(mime)};
    }
    void onProgress(uint64_t id, const DmapProgress& p) override { progress[id] = p; }
    void onSessionEnded(uint64_t id, std::string_view reason, const SessionStats&) override
    {
        ended.emplace_back(id, std::string(reason));
    }
};

struct Fixture {
    net::Reactor reactor;
    RecordingSink sink;
    tapein::platform::NullAdvertiser advertiser;
    std::shared_ptr<crypto::RaopRsaKey> key = crypto::RaopRsaKey::load().value();
    ReceiverConfig config;
    std::unique_ptr<Receiver> receiver;

    explicit Fixture(uint16_t holdBack = 2, size_t maxSessions = 100)
    {
        config.name = "Tape In Test";
        config.hardwareId = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
        config.holdBackPackets = holdBack;
        config.maxSessions = maxSessions;
        config.bindAddress = loopback(0);
        receiver = std::make_unique<Receiver>(reactor, config, sink, advertiser, key);
        REQUIRE(receiver->start().ok());
        REQUIRE(receiver->rtspPort() != 0);
    }

    void pump(std::chrono::milliseconds duration)
    {
        const auto until = Clock::now() + duration;
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
};

struct Client : tapein::test::RaopClient {
    explicit Client(Fixture& f)
        : RaopClient(f.receiver->rtspPort(), [&f](std::chrono::milliseconds ms) { f.reactor.runOnce(ms); })
    {
    }
};

} // namespace

TEST_CASE("service record is the auxCord-compatible RAOP TXT set")
{
    Fixture fx;
    const auto rec = fx.receiver->serviceRecord();
    REQUIRE(rec.type == "_raop._tcp");
    REQUIRE(rec.instanceName == "0A0B0C0D0E0F@Tape In Test");
    REQUIRE(rec.port == fx.receiver->rtspPort());
    std::map<std::string, std::string> txt(rec.txt.begin(), rec.txt.end());
    REQUIRE(txt["txtvers"] == "1");
    REQUIRE(txt["ch"] == "2");
    REQUIRE(txt["cn"] == "0,1");
    REQUIRE(txt["et"] == "0,1");
    REQUIRE(txt["ek"] == "1");
    REQUIRE(txt["sr"] == "44100");
    REQUIRE(txt["ss"] == "16");
    REQUIRE(txt["tp"] == "UDP");
    REQUIRE(txt["vn"] == "3");
    REQUIRE(txt["da"] == "true");
    REQUIRE(txt["sv"] == "false");
    REQUIRE(txt["md"] == "0,1,2");
    REQUIRE(txt["pw"] == "false");
    REQUIRE(txt["sm"] == "false");
    REQUIRE(txt["am"] == "TapeIn,1");
    REQUIRE(txt["vs"] == "130.14");
    REQUIRE(txt["src"] == fx.config.sourceUrl);
    REQUIRE(fx.advertiser.published.size() == 1);
}

TEST_CASE("OPTIONS gets Public, Server, CSeq and a correct Apple-Response")
{
    Fixture fx;
    Client c(fx);
    auto r = c.request("OPTIONS", "Apple-Challenge: AAECAwQFBgcICQoLDA0ODw\r\n", "", "*");
    REQUIRE(r.status == 200);
    REQUIRE(r.headers.get("CSeq") == "1");
    REQUIRE(r.headers.get("Server") == "AirTunes/105.1");
    REQUIRE(r.headers.get("Public") == "ANNOUNCE, SETUP, RECORD, PAUSE, FLUSH, TEARDOWN, OPTIONS, GET_PARAMETER, SET_PARAMETER");
    const std::array<uint8_t, 4> local{127, 0, 0, 1};
    const auto expected = crypto::appleChallengeResponse(*fx.key, "AAECAwQFBgcICQoLDA0ODw", local, fx.config.hardwareId);
    REQUIRE(expected.ok());
    REQUIRE(r.headers.get("Apple-Response") == expected.value());
}

TEST_CASE("Apple-Response is omitted when answering is disabled")
{
    Fixture fx;
    fx.receiver->stop();
    fx.config.answerAppleChallenge = false;
    fx.receiver = std::make_unique<Receiver>(fx.reactor, fx.config, fx.sink, fx.advertiser, fx.key);
    REQUIRE(fx.receiver->start().ok());
    Client c(fx);
    auto r = c.request("OPTIONS", "Apple-Challenge: AAECAwQFBgcICQoLDA0ODw\r\n", "", "*");
    REQUIRE(r.status == 200);
    REQUIRE_FALSE(r.headers.get("Apple-Response").has_value());
}

TEST_CASE("ANNOUNCE SETUP RECORD yields three distinct dynamic ports and starts a session")
{
    Fixture fx;
    Client c(fx);
    auto rec = c.startStream(true);
    REQUIRE(rec.status == 200);
    REQUIRE(rec.headers.get("Audio-Latency") == "11025");
    REQUIRE(c.serverPort != 0);
    REQUIRE(c.serverControlPort != 0);
    REQUIRE(c.serverTimingPort != 0);
    REQUIRE(c.serverPort != c.serverControlPort);
    REQUIRE(c.serverControlPort != c.serverTimingPort);
    REQUIRE(fx.receiver->sessionCount() == 1);
    REQUIRE(fx.sink.started.size() == 1);
    const SessionInfo& info = fx.sink.started[0];
    REQUIRE(info.clientName == "Test iPhone");
    REQUIRE(info.dacpId == c.dacpId);
    REQUIRE(info.activeRemote == "424242");
    REQUIRE(info.userAgent == "AirPlay/740.6.1");
    REQUIRE(info.pcm);
    REQUIRE_FALSE(info.encrypted);
    REQUIRE(info.peer.addressBytes()[0] == 127);
}

TEST_CASE("unencrypted L16 packets arrive at the sink in order after hold-back")
{
    Fixture fx(2);
    Client c(fx);
    REQUIRE(c.startStream(true).status == 200);
    const uint64_t id = fx.sink.started[0].id;
    for (uint16_t s = 0; s < 6; ++s) {
        c.sendAudio(s, s * kFrames, l16Payload(tone(s)));
    }
    REQUIRE(fx.pumpUntil([&] { return fx.sink.blocks[id].size() >= 4; }));
    const auto& blocks = fx.sink.blocks[id];
    REQUIRE(blocks.size() == 4); // 6 received, hold-back 2
    for (uint16_t s = 0; s < 4; ++s) {
        REQUIRE(blocks[s].seq == s);
        REQUIRE_FALSE(blocks[s].wasMissing);
        REQUIRE(blocks[s].pcm == tone(s));
    }
}

TEST_CASE("sync packets set the sender latency and timing requests go to the client timing port")
{
    Fixture fx(2);
    Client c(fx);
    REQUIRE(c.startStream(true).status == 200);
    // The receiver must send a timing request (0xD2) to our timing port shortly after RECORD.
    std::array<uint8_t, 64> buf{};
    bool gotTiming = false;
    REQUIRE(fx.pumpUntil([&] {
        auto r = c.timing.recvFrom(buf);
        if (r.ok() && r.value().first >= 32 && buf[1] == 0xD2) {
            gotTiming = true;
        }
        return gotTiming;
    }, 2000ms));
    // Send a sync packet: latency = 88200 frames.
    const std::vector<uint8_t> sync{0x90, 0xD4, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x01, 0x58, 0x88};
    REQUIRE(c.audio.sendTo(sync, loopback(c.serverControlPort)).ok());
    REQUIRE(fx.pumpUntil([&] { return fx.receiver->sessionStats(fx.sink.started[0].id).senderLatencyFrames == 88200; }));
}

TEST_CASE("ALAC packets are decrypted and decoded")
{
    Fixture fx(1);
    Client c(fx);
    const std::array<uint8_t, 16> aesKey{0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    const std::array<uint8_t, 16> iv{0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00};
    const auto wrapped = fx.key->encryptOaepSha1(aesKey);
    REQUIRE(wrapped.ok());
    REQUIRE(c.startStream(false, base64Encode(iv, true), base64Encode(wrapped.value(), true)).status == 200);
    REQUIRE(fx.sink.started[0].encrypted);
    REQUIRE_FALSE(fx.sink.started[0].pcm);
    const uint64_t id = fx.sink.started[0].id;
    for (uint16_t s = 0; s < 4; ++s) {
        const auto encrypted = aesCbcEncrypt(alacPayload(tone(s)), aesKey, iv);
        c.sendAudio(s, s * kFrames, encrypted);
    }
    REQUIRE(fx.pumpUntil([&] { return fx.sink.blocks[id].size() >= 3; }));
    for (uint16_t s = 0; s < 3; ++s) {
        REQUIRE(fx.sink.blocks[id][s].seq == s);
        REQUIRE(fx.sink.blocks[id][s].pcm == tone(s));
    }
}

TEST_CASE("missing packet triggers a resend request on the client control port")
{
    Fixture fx(10);
    Client c(fx);
    REQUIRE(c.startStream(true).status == 200);
    c.sendAudio(0, 0, l16Payload(tone(0)));
    c.sendAudio(1, kFrames, l16Payload(tone(1)));
    c.sendAudio(3, 3 * kFrames, l16Payload(tone(3))); // 2 is missing
    std::array<uint8_t, 64> buf{};
    std::vector<uint8_t> request;
    REQUIRE(fx.pumpUntil([&] {
        auto r = c.control.recvFrom(buf);
        if (r.ok() && r.value().first == 8 && buf[1] == 0xD5) {
            request.assign(buf.begin(), buf.begin() + 8);
            return true;
        }
        return false;
    }, 1500ms));
    REQUIRE(request[0] == 0x80);
    REQUIRE(request[4] == 0x00);
    REQUIRE(request[5] == 0x02); // first missing = 2
    REQUIRE(request[6] == 0x00);
    REQUIRE(request[7] == 0x01); // count = 1
    REQUIRE(fx.receiver->sessionStats(fx.sink.started[0].id).resendRequestsSent >= 1);

    // A resend reply (0x56 wrapper) fills the gap.
    std::vector<uint8_t> reply{0x80, 0x56, 0x00, 0x01};
    const auto payload = l16Payload(tone(2));
    const uint32_t ts = 2 * kFrames;
    const std::vector<uint8_t> inner{0x80, 0x60, 0x00, 0x02, static_cast<uint8_t>(ts >> 24), static_cast<uint8_t>(ts >> 16),
                                     static_cast<uint8_t>(ts >> 8), static_cast<uint8_t>(ts), 0, 0, 0, 1};
    reply.insert(reply.end(), inner.begin(), inner.end());
    reply.insert(reply.end(), payload.begin(), payload.end());
    REQUIRE(c.audio.sendTo(reply, loopback(c.serverPort)).ok());
    REQUIRE(fx.pumpUntil([&] { return fx.receiver->sessionStats(fx.sink.started[0].id).packetsReceived == 4; }));
}

TEST_CASE("FLUSH drops older packets and TEARDOWN releases the rest")
{
    Fixture fx(63);
    Client c(fx);
    REQUIRE(c.startStream(true).status == 200);
    const uint64_t id = fx.sink.started[0].id;
    for (uint16_t s = 0; s < 6; ++s) {
        c.sendAudio(s, s * kFrames, l16Payload(tone(s)));
    }
    REQUIRE(fx.pumpUntil([&] { return fx.receiver->sessionStats(id).packetsReceived == 6; }));
    REQUIRE(fx.sink.blocks[id].empty()); // hold-back of 63 keeps everything buffered
    auto f = c.request("FLUSH", "Session: 1\r\nRTP-Info: seq=3;rtptime=" + std::to_string(3 * kFrames) + "\r\n");
    REQUIRE(f.status == 200);
    auto t = c.request("TEARDOWN", "Session: 1\r\n");
    REQUIRE(t.status == 200);
    REQUIRE(t.headers.get("Connection") == "close");
    REQUIRE(fx.pumpUntil([&] { return !fx.sink.ended.empty(); }));
    const auto& blocks = fx.sink.blocks[id];
    REQUIRE(blocks.size() == 3);
    REQUIRE(blocks[0].seq == 3);
    REQUIRE(blocks[2].seq == 5);
    REQUIRE(fx.sink.ended[0].second == "teardown");
    REQUIRE(fx.receiver->sessionCount() == 0);
}

TEST_CASE("volume SET_PARAMETER reaches the sink and GET_PARAMETER echoes it")
{
    Fixture fx;
    Client c(fx);
    REQUIRE(c.startStream(true).status == 200);
    auto r = c.request("SET_PARAMETER", "Session: 1\r\nContent-Type: text/parameters\r\n", "volume: -12.5\r\n");
    REQUIRE(r.status == 200);
    REQUIRE(fx.sink.volume[fx.sink.started[0].id] == -12.5f);
    auto g = c.request("GET_PARAMETER", "Session: 1\r\nContent-Type: text/parameters\r\n", "volume\r\n");
    REQUIRE(g.status == 200);
    REQUIRE(g.body.find("volume: -12.5") != std::string::npos);
}

TEST_CASE("metadata, artwork and progress SET_PARAMETER reach the sink")
{
    Fixture fx;
    Client c(fx);
    REQUIRE(c.startStream(true).status == 200);
    const auto id = fx.sink.started[0].id;

    // DMAP metadata: a single minm tag.
    std::string dmap = "minm";
    const char name[] = "Hello";
    const uint32_t n = 5;
    dmap.push_back(static_cast<char>((n >> 24) & 0xFF));
    dmap.push_back(static_cast<char>((n >> 16) & 0xFF));
    dmap.push_back(static_cast<char>((n >> 8) & 0xFF));
    dmap.push_back(static_cast<char>(n & 0xFF));
    dmap += name;
    REQUIRE(c.request("SET_PARAMETER", "Session: 1\r\nContent-Type: application/x-dmap-tagged\r\n", dmap).status == 200);
    REQUIRE(fx.sink.metadata[id].trackName == "Hello");

    // Artwork: three JPEG bytes.
    const std::string jpeg = "\xFF\xD8\xFF";
    REQUIRE(c.request("SET_PARAMETER", "Session: 1\r\nContent-Type: image/jpeg\r\n", jpeg).status == 200);
    REQUIRE(fx.sink.artwork[id].first.size() == 3);
    REQUIRE(fx.sink.artwork[id].second == "image/jpeg");

    // Progress line.
    REQUIRE(c.request("SET_PARAMETER", "Session: 1\r\nContent-Type: text/parameters\r\n", "progress: 0/100/200\r\n").status == 200);
    REQUIRE(fx.sink.progress[id].currentFrames == 100u);
    REQUIRE(fx.sink.progress[id].endFrames == 200u);
}

TEST_CASE("teardown finalises; stray datagram after close is harmless")
{
    Fixture fx(1);
    Client c(fx);
    REQUIRE(c.startStream(true).status == 200);
    const uint16_t oldPort = c.serverPort;
    REQUIRE(c.request("TEARDOWN", "Session: 1\r\n").status == 200);
    REQUIRE(fx.pumpUntil([&] { return fx.receiver->sessionCount() == 0; }));
    c.sendAudio(0, 0, l16Payload(tone(0)));
    (void)oldPort;
    fx.pump(50ms);
    REQUIRE(fx.receiver->sessionCount() == 0);
    REQUIRE(fx.sink.blocks.empty());
    // The TCP connection is closed by the receiver after TEARDOWN.
    REQUIRE(fx.pumpUntil([&] { return c.eofSeen(); }));
}

TEST_CASE("TCP close ends the session")
{
    Fixture fx(1);
    {
        Client c(fx);
        REQUIRE(c.startStream(true).status == 200);
        REQUIRE(fx.receiver->sessionCount() == 1);
    }
    REQUIRE(fx.pumpUntil([&] { return fx.receiver->sessionCount() == 0; }));
    REQUIRE(fx.sink.ended.size() == 1);
    REQUIRE(fx.sink.ended[0].second == "connection closed");
}

TEST_CASE("two concurrent sessions get distinct ports and sinks")
{
    Fixture fx(1);
    Client a(fx);
    Client b(fx);
    REQUIRE(a.startStream(true).status == 200);
    REQUIRE(b.startStream(true).status == 200);
    REQUIRE(a.serverPort != b.serverPort);
    REQUIRE(fx.receiver->sessionCount() == 2);
    const uint64_t idA = fx.sink.started[0].id;
    const uint64_t idB = fx.sink.started[1].id;
    REQUIRE(idA != idB);
    for (uint16_t s = 0; s < 3; ++s) {
        a.sendAudio(s, s * kFrames, l16Payload(tone(s)));
        b.sendAudio(s, s * kFrames, l16Payload(tone(static_cast<uint16_t>(s + 100))));
    }
    REQUIRE(fx.pumpUntil([&] { return fx.sink.blocks[idA].size() >= 2 && fx.sink.blocks[idB].size() >= 2; }));
    REQUIRE(fx.sink.blocks[idA][0].pcm == tone(0));
    REQUIRE(fx.sink.blocks[idB][0].pcm == tone(100));
}

TEST_CASE("SETUP and RECORD before ANNOUNCE return 455")
{
    Fixture fx;
    Client c(fx);
    REQUIRE(c.request("SETUP", "Transport: RTP/AVP/UDP;unicast;control_port=1;timing_port=2\r\n").status == 455);
    REQUIRE(c.request("RECORD", "Session: 1\r\n").status == 455);
}

TEST_CASE("unknown method returns 501 and bad SDP returns 456")
{
    Fixture fx;
    Client c(fx);
    REQUIRE(c.request("PAUSE_NOT_REAL", "").status == 501);
    REQUIRE(c.request("ANNOUNCE", "Content-Type: application/sdp\r\n", "a=aesiv:AAEC\r\n").status == 456);
}

TEST_CASE("GET /info and POST /feedback return 404 so AirPlay-2-style senders fall back to classic")
{
    Fixture fx;
    Client c(fx);
    // pyatv treats any non-200 on GET /info as "device does not support /info".
    REQUIRE(c.request("GET", "", "", "/info").status == 404);
    REQUIRE(c.request("POST", "", "", "/feedback").status == 404);
    // The connection stays usable afterwards.
    REQUIRE(c.request("OPTIONS", "", "", "*").status == 200);
}

TEST_CASE("garbage on TCP closes the connection without crash")
{
    Fixture fx;
    Client c(fx);
    const std::string junk("\x80\x60\x00\x01garbage\r\n\r\n", 15);
    c.sendRaw(junk);
    REQUIRE(fx.pumpUntil([&] { return c.eofSeen(); }));
    // The receiver is still alive and accepts a new client.
    Client d(fx);
    REQUIRE(d.request("OPTIONS", "", "", "*").status == 200);
}

TEST_CASE("maxSessions=1: a second ANNOUNCE gets 453")
{
    Fixture fx(1, 1);
    Client a(fx);
    Client b(fx);
    REQUIRE(a.startStream(true).status == 200);
    auto r = b.request("ANNOUNCE", "Content-Type: application/sdp\r\n", b.announceBody(true));
    REQUIRE(r.status == 453);
}

TEST_CASE("same client reconnecting replaces its live session")
{
    Fixture fx(1);
    Client a(fx);
    REQUIRE(a.startStream(true).status == 200);
    const uint64_t first = fx.sink.started[0].id;
    Client b(fx);
    b.dacpId = a.dacpId; // same client identity
    REQUIRE(b.startStream(true).status == 200);
    REQUIRE(fx.pumpUntil([&] { return fx.sink.ended.size() == 1; }));
    REQUIRE(fx.sink.ended[0].first == first);
    REQUIRE(fx.sink.ended[0].second == "replaced");
    REQUIRE(fx.receiver->sessionCount() == 1);
}

TEST_CASE("capture callbacks see RTSP bytes both ways and RTP datagrams")
{
    Fixture fx(1);
    fx.receiver->stop();
    std::vector<std::pair<bool, std::string>> rtsp;
    std::vector<std::pair<uint8_t, size_t>> rtp;
    fx.config.captureRtsp = [&](uint64_t, bool outbound, std::span<const uint8_t> bytes) {
        rtsp.emplace_back(outbound, std::string(bytes.begin(), bytes.end()));
    };
    fx.config.captureRtp = [&](uint64_t, uint8_t socketKind, std::span<const uint8_t> bytes) {
        rtp.emplace_back(socketKind, bytes.size());
    };
    fx.receiver = std::make_unique<Receiver>(fx.reactor, fx.config, fx.sink, fx.advertiser, fx.key);
    REQUIRE(fx.receiver->start().ok());
    Client c(fx);
    REQUIRE(c.startStream(true).status == 200);
    c.sendAudio(0, 0, l16Payload(tone(0)));
    REQUIRE(fx.pumpUntil([&] { return !rtp.empty(); }));
    REQUIRE(rtp[0].first == 0); // audio socket
    REQUIRE(rtp[0].second == 12 + kSamples * 2);
    REQUIRE(rtsp.size() >= 6); // 3 requests in, 3 responses out
    REQUIRE_FALSE(rtsp[0].first);
    REQUIRE(rtsp[0].second.starts_with("ANNOUNCE"));
    REQUIRE(rtsp[1].first);
    REQUIRE(rtsp[1].second.starts_with("RTSP/1.0 200"));
}

#ifndef _WIN32
#include <sys/socket.h>
TEST_CASE("a peer that resets before accept cannot crash the receiver")
{
    Fixture fx;
    {
        auto c = net::TcpSocket::connect(loopback(fx.receiver->rtspPort()));
        REQUIRE(c.ok());
        // SO_LINGER{1,0}: close sends RST; the receiver may then accept a socket on which getpeername fails.
        linger l{1, 0};
        ::setsockopt(c.value().handle(), SOL_SOCKET, SO_LINGER, &l, sizeof l);
        c.value().close();
    }
    fx.pump(100ms);
    Client d(fx);
    REQUIRE(d.request("OPTIONS", "", "", "*").status == 200);
}
#endif

TEST_CASE("RTSP activity keeps a paused session alive; silence on both ends it and closes the connection")
{
    Fixture fx(1);
    fx.receiver->stop();
    fx.config.inactivityTimeout = 400ms;
    fx.receiver = std::make_unique<Receiver>(fx.reactor, fx.config, fx.sink, fx.advertiser, fx.key);
    REQUIRE(fx.receiver->start().ok());
    Client c(fx);
    REQUIRE(c.startStream(true).status == 200);
    // Paused phone: no audio, but RTSP keepalives.
    for (int i = 0; i < 8; ++i) {
        fx.pump(100ms);
        REQUIRE(c.request("OPTIONS", "", "", "*").status == 200);
    }
    REQUIRE(fx.receiver->sessionCount() == 1);
    // Phone gone: nothing at all.
    REQUIRE(fx.pumpUntil([&] { return fx.receiver->sessionCount() == 0; }, 2000ms));
    REQUIRE(fx.sink.ended.size() == 1);
    REQUIRE(fx.sink.ended[0].second.find("inactive") != std::string::npos);
    REQUIRE(fx.pumpUntil([&] { return c.eofSeen(); }));
}

TEST_CASE("a sequence jump resynchronises instead of wedging the session")
{
    Fixture fx(1);
    Client c(fx);
    REQUIRE(c.startStream(true).status == 200);
    const uint64_t id = fx.sink.started[0].id;
    for (uint16_t s = 0; s < 3; ++s) {
        c.sendAudio(s, s * kFrames, l16Payload(tone(s)));
    }
    REQUIRE(fx.pumpUntil([&] { return fx.sink.blocks[id].size() >= 2; }));
    // Sender restarts its sequence far away.
    for (uint16_t s = 30000; s < 30020; ++s) {
        c.sendAudio(s, s * kFrames, l16Payload(tone(static_cast<uint16_t>(s % 100))));
    }
    REQUIRE(fx.pumpUntil([&] {
        for (const auto& b : fx.sink.blocks[id]) {
            if (b.seq >= 30000) {
                return true;
            }
        }
        return false;
    }));
    REQUIRE(fx.receiver->sessionStats(id).packetsRejected >= 1);
}

TEST_CASE("a session that never reaches RECORD expires")
{
    Fixture fx(1);
    fx.receiver->stop();
    fx.config.setupTimeout = 300ms;
    fx.receiver = std::make_unique<Receiver>(fx.reactor, fx.config, fx.sink, fx.advertiser, fx.key);
    REQUIRE(fx.receiver->start().ok());
    Client c(fx);
    REQUIRE(c.request("ANNOUNCE", "Content-Type: application/sdp\r\nDACP-ID: " + c.dacpId + "\r\n", c.announceBody(true)).status == 200);
    REQUIRE(fx.receiver->sessionCount() == 1);
    REQUIRE(fx.pumpUntil([&] { return fx.receiver->sessionCount() == 0; }, 2000ms));
    REQUIRE(fx.sink.started.empty()); // never started, so never reported
    // The stalled connection is dropped; a fresh one works.
    REQUIRE(fx.pumpUntil([&] { return c.eofSeen(); }));
    Client d(fx);
    REQUIRE(d.startStream(true).status == 200);
    REQUIRE(fx.receiver->sessionCount() == 1);
}

TEST_CASE("connections beyond the cap are closed immediately")
{
    Fixture fx(1);
    fx.receiver->stop();
    fx.config.maxConnections = 2;
    fx.receiver = std::make_unique<Receiver>(fx.reactor, fx.config, fx.sink, fx.advertiser, fx.key);
    REQUIRE(fx.receiver->start().ok());
    Client a(fx);
    Client b(fx);
    REQUIRE(a.request("OPTIONS", "", "", "*").status == 200);
    REQUIRE(b.request("OPTIONS", "", "", "*").status == 200);
    Client c(fx);
    REQUIRE(fx.pumpUntil([&] { return c.eofSeen(); }));
    REQUIRE(a.request("OPTIONS", "", "", "*").status == 200);
}

TEST_CASE("RECORD before SETUP is 455")
{
    Fixture fx;
    Client c(fx);
    REQUIRE(c.request("ANNOUNCE", "Content-Type: application/sdp\r\n", c.announceBody(true)).status == 200);
    REQUIRE(c.request("RECORD", "Session: 1\r\n").status == 455);
}

TEST_CASE("stop ends all sessions and unpublishes")
{
    Fixture fx(1);
    Client a(fx);
    REQUIRE(a.startStream(true).status == 200);
    fx.receiver->stop();
    REQUIRE(fx.receiver->sessionCount() == 0);
    REQUIRE(fx.sink.ended.size() == 1);
    REQUIRE(fx.advertiser.unpublished == 1);
}
