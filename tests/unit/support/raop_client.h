// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors
//
// Scripted RAOP client for tests: RTSP over loopback plus the three UDP
// sockets, with helpers to build L16 / ALAC / encrypted payloads. The
// caller supplies a pump that runs the receiver's event loop (the test
// thread's reactor, or a sleep when the engine owns its own thread).

#pragma once

#include "tapein/core/net/socket.h"
#include "tapein/core/rtsp/rtsp_message.h"

#include <ALACEncoder.h>
#include <mbedtls/aes.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace tapein::test {

namespace net = tapein::core::net;
namespace rtsp = tapein::core::rtsp;

constexpr uint16_t kFrames = 352;
constexpr size_t kSamples = kFrames * 2;

inline net::Endpoint loopback(uint16_t port)
{
    net::Endpoint e;
    e.address = {127, 0, 0, 1};
    e.port = port;
    return e;
}

struct Response {
    int status = 0;
    rtsp::Headers headers;
    std::string body;
};

inline std::string nextDacpId()
{
    static int counter = 0;
    char buf[32];
    std::snprintf(buf, sizeof buf, "ABCDEF01234567%02X", counter++ & 0xFF);
    return buf;
}

struct RaopClient {
    using Pump = std::function<void(std::chrono::milliseconds)>;
    Pump pump; ///< runs the event loop for up to the given time
    net::TcpSocket tcp;
    net::UdpSocket control = net::UdpSocket::bind(loopback(0)).value();
    net::UdpSocket timing = net::UdpSocket::bind(loopback(0)).value();
    net::UdpSocket audio = net::UdpSocket::bind(loopback(0)).value();
    std::string inbox;
    int cseq = 0;
    std::string dacpId = nextDacpId();
    uint16_t serverPort = 0;
    uint16_t serverControlPort = 0;
    uint16_t serverTimingPort = 0;

    RaopClient(uint16_t rtspPort, Pump pumpFn) : pump(std::move(pumpFn))
    {
        auto c = net::TcpSocket::connect(loopback(rtspPort));
        REQUIRE(c.ok());
        tcp = std::move(c.value());
    }

    template <class F>
    bool pumpUntil(F&& cond, std::chrono::milliseconds timeout = std::chrono::milliseconds(3000))
    {
        const auto until = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < until) {
            if (cond()) {
                return true;
            }
            pump(std::chrono::milliseconds(5));
        }
        return cond();
    }

    void sendRaw(const std::string& s)
    {
        size_t off = 0;
        while (off < s.size()) {
            auto w = tcp.write(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(s.data()) + off, s.size() - off));
            REQUIRE(w.ok());
            off += w.value();
            if (w.value() == 0) {
                pump(std::chrono::milliseconds(1));
            }
        }
    }

    Response request(const std::string& method, const std::string& extraHeaders = "", const std::string& body = "",
                     const std::string& uri = "rtsp://127.0.0.1/1234")
    {
        std::string msg = method + " " + uri + " RTSP/1.0\r\nCSeq: " + std::to_string(++cseq) + "\r\n" + extraHeaders;
        if (!body.empty()) {
            msg += "Content-Length: " + std::to_string(body.size()) + "\r\n";
        }
        msg += "\r\n" + body;
        sendRaw(msg);
        return readResponse();
    }

    Response readResponse()
    {
        Response r;
        std::array<uint8_t, 4096> buf{};
        size_t headerEnd = std::string::npos;
        size_t bodyLen = 0;
        const bool ok = pumpUntil([&] {
            auto n = tcp.read(buf);
            if (n.ok() && n.value() > 0) {
                inbox.append(reinterpret_cast<const char*>(buf.data()), n.value());
            }
            if (headerEnd == std::string::npos) {
                headerEnd = inbox.find("\r\n\r\n");
                if (headerEnd == std::string::npos) {
                    return false;
                }
                const auto cl = inbox.find("Content-Length: ");
                if (cl != std::string::npos && cl < headerEnd) {
                    bodyLen = std::stoul(inbox.substr(cl + 16));
                }
            }
            return inbox.size() >= headerEnd + 4 + bodyLen;
        });
        REQUIRE(ok);
        const std::string head = inbox.substr(0, headerEnd);
        r.body = inbox.substr(headerEnd + 4, bodyLen);
        inbox.erase(0, headerEnd + 4 + bodyLen);
        REQUIRE(head.starts_with("RTSP/1.0 "));
        r.status = std::stoi(head.substr(9, 3));
        size_t pos = head.find("\r\n");
        while (pos != std::string::npos) {
            const size_t next = head.find("\r\n", pos + 2);
            const std::string line = head.substr(pos + 2, next == std::string::npos ? std::string::npos : next - pos - 2);
            const size_t colon = line.find(": ");
            if (colon != std::string::npos) {
                r.headers.set(line.substr(0, colon), line.substr(colon + 2));
            }
            pos = next;
        }
        return r;
    }

    bool eofSeen()
    {
        std::array<uint8_t, 64> buf{};
        auto n = tcp.read(buf);
        return !n.ok();
    }

    std::string announceBody(bool pcm, const std::string& aesivB64 = "", const std::string& keyB64 = "") const
    {
        std::string sdp = "v=0\r\no=iTunes 123 0 IN IP4 127.0.0.1\r\ns=iTunes\r\nc=IN IP4 127.0.0.1\r\nt=0 0\r\n"
                          "m=audio 0 RTP/AVP 96\r\n";
        sdp += pcm ? "a=rtpmap:96 L16/44100/2\r\n" : "a=rtpmap:96 AppleLossless\r\n";
        sdp += "a=fmtp:96 352 0 16 40 10 14 2 255 0 0 44100\r\n";
        if (!keyB64.empty()) {
            sdp += "a=rsaaeskey:" + keyB64 + "\r\n";
            sdp += "a=aesiv:" + aesivB64 + "\r\n";
        }
        return sdp;
    }

    // ANNOUNCE + SETUP + RECORD; fills server ports. Returns the RECORD response.
    Response startStream(bool pcm, const std::string& aesivB64 = "", const std::string& keyB64 = "")
    {
        auto a = request("ANNOUNCE",
                         "Content-Type: application/sdp\r\nDACP-ID: " + dacpId + "\r\nActive-Remote: 424242\r\n"
                         "User-Agent: AirPlay/740.6.1\r\nX-Apple-Client-Name: Test iPhone\r\n",
                         announceBody(pcm, aesivB64, keyB64));
        REQUIRE(a.status == 200);
        const std::string transport = "Transport: RTP/AVP/UDP;unicast;interleaved=0-1;mode=record;control_port="
                                      + std::to_string(control.localEndpoint().value().port)
                                      + ";timing_port=" + std::to_string(timing.localEndpoint().value().port) + "\r\n";
        auto s = request("SETUP", transport);
        REQUIRE(s.status == 200);
        const std::string t = std::string(s.headers.get("Transport").value_or(""));
        serverPort = static_cast<uint16_t>(std::stoul(t.substr(t.find("server_port=") + 12)));
        serverControlPort = static_cast<uint16_t>(std::stoul(t.substr(t.find("control_port=") + 13)));
        serverTimingPort = static_cast<uint16_t>(std::stoul(t.substr(t.find("timing_port=") + 12)));
        return request("RECORD", "Session: 1\r\nRange: npt=0-\r\nRTP-Info: seq=0;rtptime=0\r\n");
    }

    void sendAudio(uint16_t seq, uint32_t rtptime, std::span<const uint8_t> payload)
    {
        std::vector<uint8_t> pkt{0x80, 0x60, static_cast<uint8_t>(seq >> 8), static_cast<uint8_t>(seq),
                                 static_cast<uint8_t>(rtptime >> 24), static_cast<uint8_t>(rtptime >> 16),
                                 static_cast<uint8_t>(rtptime >> 8), static_cast<uint8_t>(rtptime), 0, 0, 0, 1};
        pkt.insert(pkt.end(), payload.begin(), payload.end());
        REQUIRE(audio.sendTo(pkt, loopback(serverPort)).ok());
    }
};

inline std::vector<int16_t> tone(uint16_t seq)
{
    std::vector<int16_t> pcm(kSamples);
    for (size_t i = 0; i < kFrames; ++i) {
        pcm[i * 2] = static_cast<int16_t>(seq * 100 + static_cast<int>(i % 50));
        pcm[i * 2 + 1] = static_cast<int16_t>(-(seq * 100 + static_cast<int>(i % 50)));
    }
    return pcm;
}

inline std::vector<uint8_t> l16Payload(const std::vector<int16_t>& pcm)
{
    std::vector<uint8_t> out;
    out.reserve(pcm.size() * 2);
    for (const int16_t s : pcm) {
        const auto u = static_cast<uint16_t>(s);
        out.push_back(static_cast<uint8_t>(u >> 8));
        out.push_back(static_cast<uint8_t>(u));
    }
    return out;
}

inline std::vector<uint8_t> alacPayload(const std::vector<int16_t>& pcm)
{
    AudioFormatDescription in{};
    in.mSampleRate = 44100.0;
    in.mFormatID = kALACFormatLinearPCM;
    in.mFormatFlags = kALACFormatFlagIsSignedInteger | kALACFormatFlagIsPacked;
    in.mBytesPerPacket = 4;
    in.mFramesPerPacket = 1;
    in.mBytesPerFrame = 4;
    in.mChannelsPerFrame = 2;
    in.mBitsPerChannel = 16;
    AudioFormatDescription out{};
    out.mSampleRate = 44100.0;
    out.mFormatID = kALACFormatAppleLossless;
    out.mFormatFlags = 1;
    out.mFramesPerPacket = kFrames;
    out.mChannelsPerFrame = 2;
    ALACEncoder enc;
    enc.SetFrameSize(kFrames);
    REQUIRE(enc.InitializeEncoder(out) == 0);
    std::vector<uint8_t> input(reinterpret_cast<const uint8_t*>(pcm.data()),
                               reinterpret_cast<const uint8_t*>(pcm.data()) + pcm.size() * 2);
    std::vector<uint8_t> output(input.size() + 64);
    int32_t n = static_cast<int32_t>(input.size());
    REQUIRE(enc.Encode(in, out, input.data(), output.data(), &n) == 0);
    output.resize(static_cast<size_t>(n));
    return output;
}

inline std::vector<uint8_t> aesCbcEncrypt(std::span<const uint8_t> plain, const std::array<uint8_t, 16>& key,
                                   const std::array<uint8_t, 16>& iv)
{
    std::vector<uint8_t> out(plain.begin(), plain.end());
    const size_t enc = plain.size() & ~static_cast<size_t>(15);
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, key.data(), 128);
    std::array<uint8_t, 16> ivCopy = iv;
    if (enc > 0) {
        mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT, enc, ivCopy.data(), plain.data(), out.data());
    }
    mbedtls_aes_free(&ctx);
    return out;
}

} // namespace tapein::test
