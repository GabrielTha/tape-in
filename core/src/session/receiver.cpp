// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/session/receiver.h"

#include "tapein/core/rtsp/sdp.h"

#include <algorithm>
#include <charconv>
#include <cstdio>

namespace tapein::core::session {

namespace {

constexpr const char* kPublicMethods =
    "ANNOUNCE, SETUP, RECORD, PAUSE, FLUSH, TEARDOWN, OPTIONS, GET_PARAMETER, SET_PARAMETER";

const char* reasonFor(int status) noexcept
{
    switch (status) {
    case 200:
        return "OK";
    case 400:
        return "Bad Request";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 453:
        return "Not Enough Bandwidth";
    case 455:
        return "Method Not Valid in This State";
    case 456:
        return "Header Field Not Valid for Resource";
    case 500:
        return "Internal Server Error";
    case 501:
        return "Not Implemented";
    default:
        return "Unknown";
    }
}

std::string hexUpper(const crypto::HardwareId& id)
{
    static constexpr char kDigits[] = "0123456789ABCDEF";
    std::string out;
    for (const uint8_t b : id) {
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 0x0F]);
    }
    return out;
}

/// Extracts "name=<digits>" from a Transport-style header value.
std::optional<uint16_t> transportParam(std::string_view transport, std::string_view name)
{
    const std::string key = std::string(name) + "=";
    const size_t pos = transport.find(key);
    if (pos == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view rest = transport.substr(pos + key.size());
    uint32_t value = 0;
    const auto [ptr, ec] = std::from_chars(rest.data(), rest.data() + rest.size(), value);
    if (ec != std::errc{} || ptr == rest.data() || value > 65535) {
        return std::nullopt;
    }
    return static_cast<uint16_t>(value);
}

std::optional<uint32_t> rtpInfoTimestamp(std::string_view rtpInfo)
{
    const size_t pos = rtpInfo.find("rtptime=");
    if (pos == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view rest = rtpInfo.substr(pos + 8);
    uint32_t value = 0;
    const auto [ptr, ec] = std::from_chars(rest.data(), rest.data() + rest.size(), value);
    if (ec != std::errc{} || ptr == rest.data()) {
        return std::nullopt;
    }
    return value;
}

std::string_view bodyView(const rtsp::Request& request)
{
    return std::string_view(reinterpret_cast<const char*>(request.body.data()), request.body.size());
}

} // namespace

Receiver::Receiver(net::Reactor& reactor, ReceiverConfig config, IPcmSink& sink, platform::IServiceAdvertiser& advertiser,
                   std::shared_ptr<crypto::RaopRsaKey> key)
    : m_reactor(reactor), m_config(std::move(config)), m_sink(sink), m_advertiser(advertiser), m_key(std::move(key))
{
}

Receiver::~Receiver()
{
    stop();
}

void Receiver::log(std::string message) const
{
    if (m_config.log) {
        m_config.log(std::move(message));
    }
}

Result<void> Receiver::start()
{
    if (m_running) {
        return makeError(ErrorCode::invalidState, "receiver already started");
    }
    net::Endpoint bind4 = m_config.bindAddress;
    bind4.isV6 = false;
    auto l4 = net::TcpListener::bind(bind4);
    if (!l4.ok()) {
        return l4.error();
    }
    m_listener4 = std::move(l4.value());
    m_port = m_listener4.localEndpoint().value().port;
    m_reactor.add(m_listener4.handle(), [this] { onAccept(m_listener4); });

    if (m_config.alsoBindIpv6) {
        net::Endpoint bind6;
        bind6.isV6 = true; // [::]
        bind6.port = m_port;
        auto l6 = net::TcpListener::bind(bind6);
        if (l6.ok()) {
            m_listener6 = std::move(l6.value());
            m_reactor.add(m_listener6.handle(), [this] { onAccept(m_listener6); });
        } else {
            log("IPv6 listener unavailable: " + l6.error().message);
        }
    }

    if (auto h = m_advertiser.pollHandle()) {
        m_reactor.add(*h, [this] { m_advertiser.processEvents(); });
    }
    if (auto p = m_advertiser.publish(serviceRecord()); !p.ok()) {
        log("service registration failed: " + p.error().message);
        return p.error();
    }
    m_running = true;
    log("receiver '" + m_config.name + "' listening on port " + std::to_string(m_port));
    return {};
}

void Receiver::stop()
{
    if (!m_running) {
        return;
    }
    m_running = false;
    for (auto& s : m_sessions) {
        s->end("stopped");
    }
    m_sessions.clear();
    for (auto& c : m_connections) {
        c->close("stopped");
    }
    m_connections.clear();
    if (auto h = m_advertiser.pollHandle()) {
        m_reactor.remove(*h);
    }
    m_advertiser.unpublish();
    m_reactor.remove(m_listener4.handle());
    m_listener4.close();
    if (m_listener6.handle() != net::kInvalidHandle) {
        m_reactor.remove(m_listener6.handle());
        m_listener6.close();
    }
}

platform::ServiceRecord Receiver::serviceRecord() const
{
    platform::ServiceRecord rec;
    rec.instanceName = hexUpper(m_config.hardwareId) + "@" + m_config.name;
    rec.type = "_raop._tcp";
    rec.port = m_port;
    rec.txt = {
        {"txtvers", "1"}, {"ch", "2"},      {"cn", "0,1"},    {"et", m_config.txtEt}, {"ek", "1"},
        {"sr", "44100"},  {"ss", "16"},     {"tp", "UDP"},    {"vn", "3"},            {"da", "true"},
        {"sv", "false"},  {"md", "0,1,2"},  {"pw", "false"},  {"sm", "false"},        {"am", "TapeIn,1"},
        {"vs", "130.14"}, {"src", m_config.sourceUrl},
    };
    return rec;
}

size_t Receiver::sessionCount() const noexcept
{
    return static_cast<size_t>(std::count_if(m_sessions.begin(), m_sessions.end(), [](const auto& s) { return !s->ended(); }));
}

SessionStats Receiver::sessionStats(uint64_t sessionId) const
{
    for (const auto& s : m_sessions) {
        if (s->info().id == sessionId) {
            return s->stats();
        }
    }
    return {};
}

Session* Receiver::findSession(uint64_t id) noexcept
{
    if (id == 0) {
        return nullptr;
    }
    for (auto& s : m_sessions) {
        if (s->info().id == id && !s->ended()) {
            return s.get();
        }
    }
    return nullptr;
}

void Receiver::endSession(uint64_t id, std::string_view reason)
{
    if (Session* s = findSession(id)) {
        s->end(reason);
        scheduleCleanup();
    }
}

void Receiver::scheduleCleanup()
{
    if (m_cleanupScheduled) {
        return;
    }
    m_cleanupScheduled = true;
    m_reactor.post([this] { cleanup(); });
}

void Receiver::cleanup()
{
    m_cleanupScheduled = false;
    std::erase_if(m_sessions, [](const auto& s) { return s->ended(); });
    std::erase_if(m_connections, [](const auto& c) { return !c->isOpen(); });
}

// ------------------------------------------------------------- connections

void Receiver::onAccept(net::TcpListener& listener)
{
    for (int i = 0; i < 8; ++i) {
        auto accepted = listener.accept();
        if (!accepted.ok()) {
            return;
        }
        accepted.value().setKeepAlive(std::chrono::seconds{20});
        const uint64_t id = m_nextConnectionId++;
        auto conn = std::make_unique<RtspConnection>(
            id, std::move(accepted.value()),
            [this](RtspConnection& c, const rtsp::Request& r) { onRequest(c, r); },
            [this](RtspConnection& c, std::string_view reason) { onConnectionClosed(c, reason); });
        m_reactor.add(conn->handle(), [ptr = conn.get()] { ptr->onReadable(); });
        log("connection " + std::to_string(id) + " from " + conn->peerEndpoint().map([](const net::Endpoint& e) { return e.toString(); }).value());
        m_connections.push_back(std::move(conn));
    }
}

void Receiver::onConnectionClosed(RtspConnection& conn, std::string_view reason)
{
    // Called before the socket closes: drop the registration while the
    // handle is still ours, so a reused descriptor can never hit this object.
    m_reactor.remove(conn.handle());
    log("connection " + std::to_string(conn.id()) + " closed: " + std::string(reason));
    if (conn.sessionId != 0) {
        endSession(conn.sessionId, reason == "teardown" ? "teardown" : "connection closed");
        conn.sessionId = 0;
    }
    scheduleCleanup();
}

rtsp::Response Receiver::makeResponse(const RtspConnection& conn, const rtsp::Request& request, int status) const
{
    rtsp::Response r;
    r.status = status;
    r.reason = reasonFor(status);
    if (const auto cseq = request.headers.get("CSeq")) {
        r.headers.set("CSeq", std::string(*cseq));
    }
    r.headers.set("Server", "AirTunes/105.1");
    if (m_config.answerAppleChallenge) {
        if (const auto challenge = request.headers.get("Apple-Challenge")) {
            const auto local = conn.localEndpoint();
            if (local.ok()) {
                auto response = crypto::appleChallengeResponse(*m_key, *challenge, local.value().addressBytes(),
                                                               m_config.hardwareId);
                if (response.ok()) {
                    r.headers.set("Apple-Response", response.value());
                    log("conn " + std::to_string(conn.id()) + " Apple-Challenge " + std::string(*challenge) + " local "
                        + local.value().toString() + " hwid " + hexUpper(m_config.hardwareId) + " -> " + response.value());
                } else {
                    log("Apple-Challenge failed: " + response.error().message);
                }
            }
        }
    }
    return r;
}

void Receiver::onRequest(RtspConnection& conn, const rtsp::Request& request)
{
    log("conn " + std::to_string(conn.id()) + " <- " + request.method + " " + request.uri);
    if (request.method == "OPTIONS") {
        handleOptions(conn, request);
    } else if (request.method == "ANNOUNCE") {
        handleAnnounce(conn, request);
    } else if (request.method == "SETUP") {
        handleSetup(conn, request);
    } else if (request.method == "RECORD") {
        handleRecord(conn, request);
    } else if (request.method == "FLUSH") {
        handleFlush(conn, request);
    } else if (request.method == "TEARDOWN") {
        handleTeardown(conn, request);
    } else if (request.method == "SET_PARAMETER") {
        handleSetParameter(conn, request);
    } else if (request.method == "GET_PARAMETER") {
        handleGetParameter(conn, request);
    } else if (request.method == "PAUSE" || request.method == "POST" || request.method == "GET") {
        conn.send(makeResponse(conn, request, 200));
    } else {
        conn.send(makeResponse(conn, request, 501));
    }
}

void Receiver::handleOptions(RtspConnection& conn, const rtsp::Request& request)
{
    auto r = makeResponse(conn, request, 200);
    r.headers.set("Public", kPublicMethods);
    conn.send(r);
}

void Receiver::handleAnnounce(RtspConnection& conn, const rtsp::Request& request)
{
    auto params = rtsp::parseAnnounceSdp(request.body);
    if (!params.ok()) {
        log("ANNOUNCE rejected: " + params.error().message);
        conn.send(makeResponse(conn, request, 456));
        return;
    }
    std::optional<std::array<uint8_t, 16>> aesKey;
    if (params.value().encrypted()) {
        auto unwrapped = m_key->decryptOaepSha1(*params.value().rsaAesKey);
        if (!unwrapped.ok() || unwrapped.value().size() != 16) {
            log("ANNOUNCE rejected: AES key unwrap failed");
            conn.send(makeResponse(conn, request, 456));
            return;
        }
        std::array<uint8_t, 16> key{};
        std::copy_n(unwrapped.value().begin(), 16, key.begin());
        aesKey = key;
    }

    conn.dacpId = std::string(request.headers.get("DACP-ID").value_or(""));
    conn.activeRemote = std::string(request.headers.get("Active-Remote").value_or(""));
    conn.userAgent = std::string(request.headers.get("User-Agent").value_or(""));
    conn.clientName = std::string(request.headers.get("X-Apple-Client-Name").value_or(""));

    // A client that announces again replaces its own live session.
    if (conn.sessionId != 0) {
        endSession(conn.sessionId, "replaced");
        conn.sessionId = 0;
    }
    if (!conn.dacpId.empty()) {
        for (auto& s : m_sessions) {
            if (!s->ended() && s->info().dacpId == conn.dacpId) {
                s->end("replaced");
                scheduleCleanup();
                for (auto& c : m_connections) {
                    if (c->sessionId == s->info().id) {
                        c->sessionId = 0;
                    }
                }
            }
        }
    }

    if (sessionCount() >= m_config.maxSessions) {
        log("ANNOUNCE refused: session limit reached");
        conn.send(makeResponse(conn, request, 453));
        return;
    }

    SessionInfo info;
    info.id = m_nextSessionId++;
    info.clientName = conn.clientName;
    info.dacpId = conn.dacpId;
    info.activeRemote = conn.activeRemote;
    info.userAgent = conn.userAgent;
    info.peer = conn.peerEndpoint().ok() ? conn.peerEndpoint().value() : net::Endpoint{};

    SessionConfig sc;
    sc.holdBackPackets = m_config.holdBackPackets;
    auto session = std::make_unique<Session>(m_reactor, info, params.value(), aesKey, m_sink, sc,
                                             [this](std::string m) { log(std::move(m)); });
    conn.sessionId = info.id;
    conn.announce = params.value();
    conn.aesKey = aesKey;
    m_sessions.push_back(std::move(session));
    log("session " + std::to_string(info.id) + " announced by '" + info.clientName + "' (" + info.userAgent + ")"
        + (aesKey ? ", encrypted" : ", clear") + (params.value().isPcmL16 ? ", L16" : ", ALAC"));
    conn.send(makeResponse(conn, request, 200));
}

void Receiver::handleSetup(RtspConnection& conn, const rtsp::Request& request)
{
    Session* session = findSession(conn.sessionId);
    if (session == nullptr) {
        conn.send(makeResponse(conn, request, 455));
        return;
    }
    const auto transport = request.headers.get("Transport").value_or("");
    const auto controlPort = transportParam(transport, "control_port");
    const auto timingPort = transportParam(transport, "timing_port");
    if (!controlPort || !timingPort) {
        conn.send(makeResponse(conn, request, 400));
        return;
    }
    const auto local = conn.localEndpoint();
    if (!local.ok()) {
        conn.send(makeResponse(conn, request, 500));
        return;
    }
    if (auto r = session->setup(local.value(), *controlPort, *timingPort); !r.ok()) {
        log("SETUP failed: " + r.error().message);
        conn.send(makeResponse(conn, request, 500));
        return;
    }
    auto response = makeResponse(conn, request, 200);
    response.headers.set("Transport", "RTP/AVP/UDP;unicast;interleaved=0-1;mode=record;control_port="
                                          + std::to_string(session->controlPort())
                                          + ";timing_port=" + std::to_string(session->timingPort())
                                          + ";server_port=" + std::to_string(session->serverPort()));
    response.headers.set("Session", "1");
    conn.send(response);
}

void Receiver::handleRecord(RtspConnection& conn, const rtsp::Request& request)
{
    Session* session = findSession(conn.sessionId);
    if (session == nullptr) {
        conn.send(makeResponse(conn, request, 455));
        return;
    }
    session->record();
    if (const auto rtpInfo = request.headers.get("RTP-Info")) {
        if (const auto ts = rtpInfoTimestamp(*rtpInfo)) {
            session->flush(*ts);
        }
    }
    auto response = makeResponse(conn, request, 200);
    response.headers.set("Audio-Latency", "11025");
    conn.send(response);
}

void Receiver::handleFlush(RtspConnection& conn, const rtsp::Request& request)
{
    Session* session = findSession(conn.sessionId);
    if (session == nullptr) {
        conn.send(makeResponse(conn, request, 455));
        return;
    }
    if (const auto rtpInfo = request.headers.get("RTP-Info")) {
        if (const auto ts = rtpInfoTimestamp(*rtpInfo)) {
            session->flush(*ts);
        }
    }
    conn.send(makeResponse(conn, request, 200));
}

void Receiver::handleTeardown(RtspConnection& conn, const rtsp::Request& request)
{
    if (conn.sessionId != 0) {
        endSession(conn.sessionId, "teardown");
        conn.sessionId = 0;
    }
    auto response = makeResponse(conn, request, 200);
    response.headers.set("Connection", "close");
    conn.send(response);
    conn.close("teardown");
}

void Receiver::handleSetParameter(RtspConnection& conn, const rtsp::Request& request)
{
    const auto contentType = request.headers.get("Content-Type").value_or("");
    Session* session = findSession(conn.sessionId);
    if (contentType.starts_with("text/parameters") && session != nullptr) {
        const std::string_view body = bodyView(request);
        size_t pos = 0;
        while (pos < body.size()) {
            size_t end = body.find('\n', pos);
            if (end == std::string_view::npos) {
                end = body.size();
            }
            std::string_view line = body.substr(pos, end - pos);
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }
            if (line.starts_with("volume:")) {
                const std::string value(line.substr(7));
                char* endPtr = nullptr;
                const float dB = std::strtof(value.c_str(), &endPtr);
                if (endPtr != value.c_str()) {
                    session->setVolume(std::clamp(dB, -144.0f, 0.0f));
                }
            }
            pos = end + 1;
        }
    }
    conn.send(makeResponse(conn, request, 200));
}

void Receiver::handleGetParameter(RtspConnection& conn, const rtsp::Request& request)
{
    auto response = makeResponse(conn, request, 200);
    const std::string_view body = bodyView(request);
    Session* session = findSession(conn.sessionId);
    if (body.starts_with("volume") && session != nullptr) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "volume: %.6f\r\n", static_cast<double>(session->volume()));
        const std::string text(buf);
        response.body.assign(text.begin(), text.end());
        response.headers.set("Content-Type", "text/parameters");
    }
    conn.send(response);
}

} // namespace tapein::core::session
