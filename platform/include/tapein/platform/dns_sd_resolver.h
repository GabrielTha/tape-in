// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/net/reactor.h"
#include "tapein/core/platform/service_resolver.h"

#include <cstdint>
#include <string>

namespace tapein::platform {

/// Resolves a DNS-SD instance through the system mDNSResponder (`dns_sd.h`),
/// then looks up its address. Self-driving: it registers its DNSServiceRef
/// socket on the reactor for the duration of a resolve, so callers only call
/// resolve(). pollHandle() returns nullopt (nothing for an owner to poll).
class DnsSdResolver final : public core::platform::IServiceResolver {
public:
    explicit DnsSdResolver(core::net::Reactor& reactor);
    ~DnsSdResolver() override;
    DnsSdResolver(const DnsSdResolver&) = delete;
    DnsSdResolver& operator=(const DnsSdResolver&) = delete;

    void resolve(std::string instanceName, std::string type, Callback cb) override;
    [[nodiscard]] std::optional<core::net::NativeHandle> pollHandle() const override { return std::nullopt; }
    void processEvents() override;
    void cancel() override;

private:
    // Matches DNSServiceResolveReply; cast at the DNSServiceResolve call.
    static void onResolveReply(void* ref, unsigned int flags, unsigned int interfaceIndex, int error,
                               const char* fullname, const char* hosttarget, uint16_t port, uint16_t txtLen,
                               const unsigned char* txt, void* context);
    void onFdReadable();
    void finishResolve();
    void teardown();
    void deliver(core::Result<core::platform::ResolvedService> result);

    core::net::Reactor& m_reactor;
    void* m_ref = nullptr; // DNSServiceRef
    int m_fd = -1;
    Callback m_cb;
    std::string m_instance;

    bool m_haveReply = false;
    int m_replyError = 0;
    std::string m_host;
    uint16_t m_port = 0;
    uint32_t m_interfaceIndex = 0;
};

} // namespace tapein::platform
