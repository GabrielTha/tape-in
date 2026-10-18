// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/util/result.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

namespace tapein::core::net {

#ifdef _WIN32
using NativeHandle = std::uintptr_t; // SOCKET
inline constexpr NativeHandle kInvalidHandle = ~static_cast<NativeHandle>(0);
#else
using NativeHandle = int;
inline constexpr NativeHandle kInvalidHandle = -1;
#endif

/// IPv4 or IPv6 address plus port. Address bytes are in network order;
/// IPv4 uses the first 4 bytes.
struct Endpoint {
    std::array<uint8_t, 16> address{};
    bool isV6 = false;
    uint16_t port = 0;
    uint32_t scopeId = 0; ///< IPv6 link-local zone (sin6_scope_id); 0 otherwise

    [[nodiscard]] std::span<const uint8_t> addressBytes() const noexcept
    {
        return std::span<const uint8_t>(address.data(), isV6 ? 16u : 4u);
    }
    [[nodiscard]] std::string toString() const;
};

/// All sockets are non-blocking. "Nothing available" is reported as
/// 0 bytes with ok() for reads, or ErrorCode::wouldBlock for accept.
class TcpSocket {
public:
    TcpSocket() = default;
    explicit TcpSocket(NativeHandle h) noexcept : m_handle(h) {}
    TcpSocket(TcpSocket&& o) noexcept : m_handle(std::exchange(o.m_handle, kInvalidHandle)) {}
    TcpSocket& operator=(TcpSocket&& o) noexcept;
    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;
    ~TcpSocket() { close(); }

    /// Blocking connect, then switches the socket to non-blocking.
    static Result<TcpSocket> connect(const Endpoint& remote);
    /// Non-blocking connect bounded by `timeout`, then leaves the socket
    /// non-blocking. Returns ErrorCode::wouldBlock if the connect did not
    /// complete in time (so a dead peer never stalls the caller).
    static Result<TcpSocket> connect(const Endpoint& remote, std::chrono::milliseconds timeout);

    Result<size_t> read(std::span<uint8_t> buffer); ///< EOF → ioError
    Result<size_t> write(std::span<const uint8_t> data); ///< may be partial; 0 if would block
    Result<Endpoint> localEndpoint() const;
    Result<Endpoint> peerEndpoint() const;
    void setKeepAlive(std::chrono::seconds idle) noexcept;
    [[nodiscard]] NativeHandle handle() const noexcept { return m_handle; }
    [[nodiscard]] bool isOpen() const noexcept { return m_handle != kInvalidHandle; }
    void close() noexcept;

private:
    NativeHandle m_handle = kInvalidHandle;
};

class TcpListener {
public:
    TcpListener() = default;
    TcpListener(TcpListener&& o) noexcept : m_handle(std::exchange(o.m_handle, kInvalidHandle)) {}
    TcpListener& operator=(TcpListener&& o) noexcept;
    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;
    ~TcpListener() { close(); }

    /// port 0 = let the OS pick; read it back with localEndpoint().
    static Result<TcpListener> bind(const Endpoint& local);
    Result<TcpSocket> accept(); ///< wouldBlock when nothing is pending
    Result<Endpoint> localEndpoint() const;
    [[nodiscard]] NativeHandle handle() const noexcept { return m_handle; }
    void close() noexcept;

private:
    explicit TcpListener(NativeHandle h) noexcept : m_handle(h) {}
    NativeHandle m_handle = kInvalidHandle;
};

class UdpSocket {
public:
    UdpSocket() = default;
    UdpSocket(UdpSocket&& o) noexcept : m_handle(std::exchange(o.m_handle, kInvalidHandle)) {}
    UdpSocket& operator=(UdpSocket&& o) noexcept;
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    ~UdpSocket() { close(); }

    static Result<UdpSocket> bind(const Endpoint& local);
    Result<size_t> sendTo(std::span<const uint8_t> data, const Endpoint& to);
    /// 0 bytes with ok() when nothing is available.
    Result<std::pair<size_t, Endpoint>> recvFrom(std::span<uint8_t> buffer);
    Result<Endpoint> localEndpoint() const;
    [[nodiscard]] NativeHandle handle() const noexcept { return m_handle; }
    [[nodiscard]] bool isOpen() const noexcept { return m_handle != kInvalidHandle; }
    void close() noexcept;

private:
    explicit UdpSocket(NativeHandle h) noexcept : m_handle(h) {}
    NativeHandle m_handle = kInvalidHandle;
};

} // namespace tapein::core::net
