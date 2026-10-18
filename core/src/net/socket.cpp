// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors
//
// The one file in the core with platform-conditional code: BSD sockets on
// POSIX, Winsock on Windows. Everything above this layer is portable.

#include "tapein/core/net/socket.h"

#include <cerrno>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace tapein::core::net {

namespace {

#ifdef _WIN32
using SockLen = int;
using SockHandle = SOCKET;
using IoLen = int;
struct WinsockInit {
    WinsockInit()
    {
        WSADATA data{};
        WSAStartup(MAKEWORD(2, 2), &data);
    }
    ~WinsockInit() { WSACleanup(); }
};
void ensureWinsock()
{
    static WinsockInit init;
}
int lastError() { return WSAGetLastError(); }
bool isWouldBlock(int e) { return e == WSAEWOULDBLOCK; }
bool isInterrupted(int e) { return e == WSAEINTR; }
bool isInProgress(int e) { return e == WSAEWOULDBLOCK; }
int pollWritable(NativeHandle h, int timeoutMs)
{
    WSAPOLLFD pfd{static_cast<SOCKET>(h), POLLWRNORM, 0};
    return WSAPoll(&pfd, 1, timeoutMs);
}
void closeHandle(NativeHandle h) { closesocket(static_cast<SOCKET>(h)); }
bool setNonBlocking(NativeHandle h)
{
    u_long on = 1;
    return ioctlsocket(static_cast<SOCKET>(h), FIONBIO, &on) == 0;
}
#else
using SockLen = socklen_t;
using SockHandle = int;
using IoLen = size_t;
void ensureWinsock() {}
int lastError() { return errno; }
bool isWouldBlock(int e) { return e == EWOULDBLOCK || e == EAGAIN; }
bool isInterrupted(int e) { return e == EINTR; }
bool isInProgress(int e) { return e == EINPROGRESS; }
int pollWritable(NativeHandle h, int timeoutMs)
{
    pollfd pfd{static_cast<int>(h), POLLOUT, 0};
    return ::poll(&pfd, 1, timeoutMs);
}
void closeHandle(NativeHandle h) { ::close(h); }
bool setNonBlocking(NativeHandle h)
{
    const int flags = fcntl(h, F_GETFL, 0);
    return flags >= 0 && fcntl(h, F_SETFL, flags | O_NONBLOCK) == 0;
}
#endif

std::string errorText(int e)
{
#ifdef _WIN32
    char buf[256] = {};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, static_cast<DWORD>(e), 0, buf,
                   sizeof buf, nullptr);
    return std::string(buf);
#else
    return std::strerror(e);
#endif
}

Error socketError(const char* what)
{
    const int e = lastError();
    return makeError(ErrorCode::ioError, std::string(what) + ": " + errorText(e));
}

SockLen toSockaddr(const Endpoint& ep, sockaddr_storage& out) noexcept
{
    std::memset(&out, 0, sizeof out);
    if (ep.isV6) {
        auto* a = reinterpret_cast<sockaddr_in6*>(&out);
        a->sin6_family = AF_INET6;
        a->sin6_port = htons(ep.port);
        a->sin6_scope_id = ep.scopeId;
        std::memcpy(&a->sin6_addr, ep.address.data(), 16);
        return sizeof(sockaddr_in6);
    }
    auto* a = reinterpret_cast<sockaddr_in*>(&out);
    a->sin_family = AF_INET;
    a->sin_port = htons(ep.port);
    std::memcpy(&a->sin_addr, ep.address.data(), 4);
    return sizeof(sockaddr_in);
}

Endpoint fromSockaddr(const sockaddr_storage& in) noexcept
{
    Endpoint ep;
    if (in.ss_family == AF_INET6) {
        const auto* a = reinterpret_cast<const sockaddr_in6*>(&in);
        ep.isV6 = true;
        ep.port = ntohs(a->sin6_port);
        ep.scopeId = a->sin6_scope_id;
        std::memcpy(ep.address.data(), &a->sin6_addr, 16);
    } else {
        const auto* a = reinterpret_cast<const sockaddr_in*>(&in);
        ep.isV6 = false;
        ep.port = ntohs(a->sin_port);
        std::memcpy(ep.address.data(), &a->sin_addr, 4);
    }
    return ep;
}

Result<Endpoint> queryName(NativeHandle h, bool peer)
{
    sockaddr_storage ss{};
    SockLen len = sizeof ss;
    const int rc = peer ? ::getpeername(static_cast<SockHandle>(h), reinterpret_cast<sockaddr*>(&ss), &len)
                        : ::getsockname(static_cast<SockHandle>(h), reinterpret_cast<sockaddr*>(&ss), &len);
    if (rc != 0) {
        return socketError(peer ? "getpeername" : "getsockname");
    }
    return fromSockaddr(ss);
}

Result<NativeHandle> openSocket(bool v6, int type)
{
    ensureWinsock();
    const SockHandle s = ::socket(v6 ? AF_INET6 : AF_INET, type, 0);
#ifdef _WIN32
    if (s == INVALID_SOCKET) {
#else
    if (s < 0) {
#endif
        return socketError("socket");
    }
    const auto h = static_cast<NativeHandle>(s);
    if (v6) {
        int on = 1;
        ::setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&on), sizeof on);
    }
#ifdef SO_NOSIGPIPE
    {
        int on = 1;
        ::setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
    }
#endif
    return h;
}

Result<void> bindHandle(NativeHandle h, const Endpoint& local)
{
    sockaddr_storage ss{};
    const SockLen len = toSockaddr(local, ss);
    if (::bind(static_cast<SockHandle>(h), reinterpret_cast<const sockaddr*>(&ss), len) != 0) {
        return socketError("bind");
    }
    return {};
}

#ifdef _WIN32
constexpr int kSendFlags = 0;
#elif defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

} // namespace

// ---------------------------------------------------------------- Endpoint

std::string Endpoint::toString() const
{
    char buf[INET6_ADDRSTRLEN] = {};
    if (isV6) {
        in6_addr a{};
        std::memcpy(&a, address.data(), 16);
        inet_ntop(AF_INET6, &a, buf, sizeof buf);
        return "[" + std::string(buf) + "]:" + std::to_string(port);
    }
    in_addr a{};
    std::memcpy(&a, address.data(), 4);
    inet_ntop(AF_INET, &a, buf, sizeof buf);
    return std::string(buf) + ":" + std::to_string(port);
}

// --------------------------------------------------------------- TcpSocket

TcpSocket& TcpSocket::operator=(TcpSocket&& o) noexcept
{
    if (this != &o) {
        close();
        m_handle = std::exchange(o.m_handle, kInvalidHandle);
    }
    return *this;
}

void TcpSocket::close() noexcept
{
    if (m_handle != kInvalidHandle) {
        closeHandle(m_handle);
        m_handle = kInvalidHandle;
    }
}

Result<TcpSocket> TcpSocket::connect(const Endpoint& remote)
{
    auto h = openSocket(remote.isV6, SOCK_STREAM);
    if (!h.ok()) {
        return h.error();
    }
    TcpSocket s(h.value());
    sockaddr_storage ss{};
    const SockLen len = toSockaddr(remote, ss);
    if (::connect(static_cast<SockHandle>(s.m_handle), reinterpret_cast<const sockaddr*>(&ss), len) != 0) {
        return socketError("connect");
    }
    if (!setNonBlocking(s.m_handle)) {
        return socketError("set non-blocking");
    }
    int on = 1;
    ::setsockopt(static_cast<SockHandle>(s.m_handle), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof on);
    return s;
}

Result<TcpSocket> TcpSocket::connect(const Endpoint& remote, std::chrono::milliseconds timeout)
{
    auto h = openSocket(remote.isV6, SOCK_STREAM);
    if (!h.ok()) {
        return h.error();
    }
    TcpSocket s(h.value());
    if (!setNonBlocking(s.m_handle)) {
        return socketError("set non-blocking");
    }
    sockaddr_storage ss{};
    const SockLen len = toSockaddr(remote, ss);
    if (::connect(static_cast<SockHandle>(s.m_handle), reinterpret_cast<const sockaddr*>(&ss), len) != 0) {
        const int e = lastError();
        if (!isInProgress(e)) {
            return socketError("connect"); // refused / unreachable right away
        }
        // Wait, bounded, for the connect to complete.
        const int ms = timeout.count() > 0 ? static_cast<int>(timeout.count()) : 0;
        int pr = 0;
        do {
            pr = pollWritable(s.m_handle, ms);
        } while (pr < 0 && isInterrupted(lastError()));
        if (pr == 0) {
            return makeError(ErrorCode::wouldBlock, "connect timed out");
        }
        if (pr < 0) {
            return socketError("connect poll");
        }
        int soErr = 0;
        SockLen soLen = sizeof soErr;
        if (::getsockopt(static_cast<SockHandle>(s.m_handle), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soErr),
                         &soLen)
                != 0) {
            return socketError("getsockopt SO_ERROR");
        }
        if (soErr != 0) {
            return makeError(ErrorCode::ioError, "connect failed");
        }
    }
    int on = 1;
    ::setsockopt(static_cast<SockHandle>(s.m_handle), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof on);
    return s;
}

Result<size_t> TcpSocket::read(std::span<uint8_t> buffer)
{
    if (buffer.empty()) {
        return size_t{0};
    }
    for (;;) {
        const auto n = ::recv(static_cast<SockHandle>(m_handle), reinterpret_cast<char*>(buffer.data()),
                              static_cast<IoLen>(buffer.size()), 0);
        if (n > 0) {
            return static_cast<size_t>(n);
        }
        if (n == 0) {
            return makeError(ErrorCode::ioError, "connection closed by peer");
        }
        const int e = lastError();
        if (isInterrupted(e)) {
            continue;
        }
        if (isWouldBlock(e)) {
            return size_t{0};
        }
        return makeError(ErrorCode::ioError, "recv: " + errorText(e));
    }
}

Result<size_t> TcpSocket::write(std::span<const uint8_t> data)
{
    if (data.empty()) {
        return size_t{0};
    }
    for (;;) {
        const auto n = ::send(static_cast<SockHandle>(m_handle), reinterpret_cast<const char*>(data.data()),
                              static_cast<IoLen>(data.size()), kSendFlags);
        if (n >= 0) {
            return static_cast<size_t>(n);
        }
        const int e = lastError();
        if (isInterrupted(e)) {
            continue;
        }
        if (isWouldBlock(e)) {
            return size_t{0};
        }
        return makeError(ErrorCode::ioError, "send: " + errorText(e));
    }
}

Result<Endpoint> TcpSocket::localEndpoint() const
{
    return queryName(m_handle, false);
}

Result<Endpoint> TcpSocket::peerEndpoint() const
{
    return queryName(m_handle, true);
}

void TcpSocket::setKeepAlive(std::chrono::seconds idle) noexcept
{
    int on = 1;
    ::setsockopt(static_cast<SockHandle>(m_handle), SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char*>(&on), sizeof on);
    const int idleSeconds = static_cast<int>(idle.count());
    const int intervalSeconds = 5;
    const int probes = 6; // a vanished peer is detected ~idle + 30 s later
    const auto* idlePtr = reinterpret_cast<const char*>(&idleSeconds);
    const auto* intervalPtr = reinterpret_cast<const char*>(&intervalSeconds);
    const auto* probesPtr = reinterpret_cast<const char*>(&probes);
#if defined(TCP_KEEPALIVE) && defined(__APPLE__)
    ::setsockopt(static_cast<SockHandle>(m_handle), IPPROTO_TCP, TCP_KEEPALIVE, idlePtr, sizeof idleSeconds);
#elif defined(TCP_KEEPIDLE)
    ::setsockopt(static_cast<SockHandle>(m_handle), IPPROTO_TCP, TCP_KEEPIDLE, idlePtr, sizeof idleSeconds);
#endif
#if defined(TCP_KEEPINTVL)
    ::setsockopt(static_cast<SockHandle>(m_handle), IPPROTO_TCP, TCP_KEEPINTVL, intervalPtr, sizeof intervalSeconds);
#else
    (void)intervalPtr;
#endif
#if defined(TCP_KEEPCNT)
    ::setsockopt(static_cast<SockHandle>(m_handle), IPPROTO_TCP, TCP_KEEPCNT, probesPtr, sizeof probes);
#else
    (void)probesPtr;
#endif
}

// ------------------------------------------------------------- TcpListener

TcpListener& TcpListener::operator=(TcpListener&& o) noexcept
{
    if (this != &o) {
        close();
        m_handle = std::exchange(o.m_handle, kInvalidHandle);
    }
    return *this;
}

void TcpListener::close() noexcept
{
    if (m_handle != kInvalidHandle) {
        closeHandle(m_handle);
        m_handle = kInvalidHandle;
    }
}

Result<TcpListener> TcpListener::bind(const Endpoint& local)
{
    auto h = openSocket(local.isV6, SOCK_STREAM);
    if (!h.ok()) {
        return h.error();
    }
    TcpListener l(h.value());
#ifndef _WIN32
    int on = 1;
    ::setsockopt(l.m_handle, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
#endif
    if (auto r = bindHandle(l.m_handle, local); !r.ok()) {
        return r.error();
    }
    if (::listen(static_cast<SockHandle>(l.m_handle), 16) != 0) {
        return socketError("listen");
    }
    if (!setNonBlocking(l.m_handle)) {
        return socketError("set non-blocking");
    }
    return l;
}

Result<TcpSocket> TcpListener::accept()
{
    for (;;) {
        sockaddr_storage ss{};
        SockLen len = sizeof ss;
        const SockHandle s = ::accept(static_cast<SockHandle>(m_handle), reinterpret_cast<sockaddr*>(&ss), &len);
#ifdef _WIN32
        if (s != INVALID_SOCKET) {
#else
        if (s >= 0) {
#endif
            TcpSocket sock(static_cast<NativeHandle>(s));
            if (!setNonBlocking(sock.handle())) {
                return socketError("set non-blocking");
            }
            int on = 1;
            ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof on);
#ifdef SO_NOSIGPIPE
            ::setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
            return sock;
        }
        const int e = lastError();
        if (isInterrupted(e)) {
            continue;
        }
        if (isWouldBlock(e)) {
            return makeError(ErrorCode::wouldBlock, "no pending connection");
        }
        return makeError(ErrorCode::ioError, "accept: " + errorText(e));
    }
}

Result<Endpoint> TcpListener::localEndpoint() const
{
    return queryName(m_handle, false);
}

// --------------------------------------------------------------- UdpSocket

UdpSocket& UdpSocket::operator=(UdpSocket&& o) noexcept
{
    if (this != &o) {
        close();
        m_handle = std::exchange(o.m_handle, kInvalidHandle);
    }
    return *this;
}

void UdpSocket::close() noexcept
{
    if (m_handle != kInvalidHandle) {
        closeHandle(m_handle);
        m_handle = kInvalidHandle;
    }
}

Result<UdpSocket> UdpSocket::bind(const Endpoint& local)
{
    auto h = openSocket(local.isV6, SOCK_DGRAM);
    if (!h.ok()) {
        return h.error();
    }
    UdpSocket s(h.value());
    if (auto r = bindHandle(s.m_handle, local); !r.ok()) {
        return r.error();
    }
    if (!setNonBlocking(s.m_handle)) {
        return socketError("set non-blocking");
    }
    const int rcvbuf = 1024 * 1024; // audio bursts after a stall must not be dropped by the kernel
    ::setsockopt(static_cast<SockHandle>(s.m_handle), SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rcvbuf),
                 sizeof rcvbuf);
    return s;
}

Result<size_t> UdpSocket::sendTo(std::span<const uint8_t> data, const Endpoint& to)
{
    sockaddr_storage ss{};
    const SockLen len = toSockaddr(to, ss);
    for (;;) {
        const auto n = ::sendto(static_cast<SockHandle>(m_handle), reinterpret_cast<const char*>(data.data()),
                                static_cast<IoLen>(data.size()), kSendFlags, reinterpret_cast<const sockaddr*>(&ss), len);
        if (n >= 0) {
            return static_cast<size_t>(n);
        }
        const int e = lastError();
        if (isInterrupted(e)) {
            continue;
        }
        if (isWouldBlock(e)) {
            return size_t{0};
        }
        return makeError(ErrorCode::ioError, "sendto: " + errorText(e));
    }
}

Result<std::pair<size_t, Endpoint>> UdpSocket::recvFrom(std::span<uint8_t> buffer)
{
    for (;;) {
        sockaddr_storage ss{};
        SockLen len = sizeof ss;
        const auto n = ::recvfrom(static_cast<SockHandle>(m_handle), reinterpret_cast<char*>(buffer.data()),
                                  static_cast<IoLen>(buffer.size()), 0, reinterpret_cast<sockaddr*>(&ss), &len);
        if (n >= 0) {
            return std::make_pair(static_cast<size_t>(n), fromSockaddr(ss));
        }
        const int e = lastError();
        if (isInterrupted(e)) {
            continue;
        }
        if (isWouldBlock(e)) {
            return std::make_pair(size_t{0}, Endpoint{});
        }
#ifdef _WIN32
        if (e == WSAECONNRESET || e == WSAEMSGSIZE) {
            // ICMP port-unreachable from an earlier send, or an oversized
            // datagram: both are ignorable for a UDP receiver.
            return std::make_pair(size_t{0}, Endpoint{});
        }
#endif
        return makeError(ErrorCode::ioError, "recvfrom: " + errorText(e));
    }
}

Result<Endpoint> UdpSocket::localEndpoint() const
{
    return queryName(m_handle, false);
}

} // namespace tapein::core::net
