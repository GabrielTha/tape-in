// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/platform/dns_sd_resolver.h"

#include <dns_sd.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cstring>

namespace tapein::platform {

using core::ErrorCode;
using core::makeError;
using core::platform::ResolvedService;

DnsSdResolver::DnsSdResolver(core::net::Reactor& reactor) : m_reactor(reactor) {}

DnsSdResolver::~DnsSdResolver()
{
    teardown();
}

void DnsSdResolver::resolve(std::string instanceName, std::string type, Callback cb)
{
    cancel();
    m_cb = std::move(cb);
    m_instance = instanceName;
    m_haveReply = false;
    m_replyError = 0;

    DNSServiceRef ref = nullptr;
    const DNSServiceErrorType err =
        DNSServiceResolve(&ref, 0, kDNSServiceInterfaceIndexAny, instanceName.c_str(), type.c_str(), "local.",
                          reinterpret_cast<DNSServiceResolveReply>(&DnsSdResolver::onResolveReply), this);
    if (err != kDNSServiceErr_NoError) {
        deliver(makeError(ErrorCode::ioError, "DNSServiceResolve failed"));
        return;
    }
    m_ref = ref;
    const int fd = DNSServiceRefSockFD(ref);
    if (fd < 0) {
        deliver(makeError(ErrorCode::ioError, "no DNSServiceRef socket"));
        return;
    }
    m_fd = fd;
    m_reactor.add(static_cast<core::net::NativeHandle>(fd), [this] { onFdReadable(); });
}

void DnsSdResolver::onResolveReply(void*, unsigned int, unsigned int interfaceIndex, int error, const char*,
                                   const char* hosttarget, uint16_t port, uint16_t, const unsigned char*, void* context)
{
    auto* self = static_cast<DnsSdResolver*>(context);
    self->m_replyError = error;
    if (error == kDNSServiceErr_NoError) {
        self->m_host = hosttarget != nullptr ? hosttarget : "";
        self->m_port = ntohs(port);
        self->m_interfaceIndex = interfaceIndex;
        self->m_haveReply = true;
    }
}

void DnsSdResolver::onFdReadable()
{
    if (m_ref == nullptr) {
        return;
    }
    const DNSServiceErrorType err = DNSServiceProcessResult(static_cast<DNSServiceRef>(m_ref));
    if (err != kDNSServiceErr_NoError) {
        deliver(makeError(ErrorCode::ioError, "DNSServiceProcessResult failed"));
        return;
    }
    if (m_haveReply) {
        finishResolve();
    } else if (m_replyError != kDNSServiceErr_NoError) {
        deliver(makeError(ErrorCode::notFound, "resolve reply error"));
    }
}

void DnsSdResolver::finishResolve()
{
    // Turn the SRV host target into an address. getaddrinfo resolves .local
    // names through mDNSResponder on macOS; it is cached and quick here.
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    const std::string host = m_host;
    const uint16_t port = m_port;
    const uint32_t ifIndex = m_interfaceIndex;
    const std::string instance = m_instance;

    const int rc = getaddrinfo(host.c_str(), nullptr, &hints, &result);
    if (rc != 0 || result == nullptr) {
        if (result != nullptr) {
            freeaddrinfo(result);
        }
        deliver(makeError(ErrorCode::notFound, "getaddrinfo failed for " + host));
        return;
    }

    core::net::Endpoint endpoint;
    endpoint.port = port;
    if (result->ai_family == AF_INET6) {
        const auto* sin6 = reinterpret_cast<const sockaddr_in6*>(result->ai_addr);
        endpoint.isV6 = true;
        std::memcpy(endpoint.address.data(), &sin6->sin6_addr, 16);
        endpoint.scopeId = sin6->sin6_scope_id != 0 ? sin6->sin6_scope_id : ifIndex;
    } else {
        const auto* sin = reinterpret_cast<const sockaddr_in*>(result->ai_addr);
        endpoint.isV6 = false;
        std::memcpy(endpoint.address.data(), &sin->sin_addr, 4);
    }
    freeaddrinfo(result);

    deliver(ResolvedService{instance, host, endpoint});
}

void DnsSdResolver::teardown()
{
    if (m_fd >= 0) {
        m_reactor.remove(static_cast<core::net::NativeHandle>(m_fd));
        m_fd = -1;
    }
    if (m_ref != nullptr) {
        DNSServiceRefDeallocate(static_cast<DNSServiceRef>(m_ref));
        m_ref = nullptr;
    }
}

void DnsSdResolver::deliver(core::Result<ResolvedService> result)
{
    teardown();
    if (m_cb) {
        auto cb = std::move(m_cb);
        m_cb = nullptr;
        cb(std::move(result));
    }
}

void DnsSdResolver::processEvents()
{
    onFdReadable();
}

void DnsSdResolver::cancel()
{
    teardown();
    m_cb = nullptr;
}

} // namespace tapein::platform
