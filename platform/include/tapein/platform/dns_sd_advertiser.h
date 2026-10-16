// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/platform/service_advertiser.h"

namespace tapein::platform {

/// DNS-SD registration through the system mDNSResponder (`dns_sd.h`).
/// Available on macOS (and anywhere Bonjour/Avahi-compat provides dns_sd).
/// The advertiser's socket is handed to the reactor via pollHandle();
/// processEvents() must be called when it is readable.
class DnsSdAdvertiser final : public core::platform::IServiceAdvertiser {
public:
    DnsSdAdvertiser() = default;
    ~DnsSdAdvertiser() override;
    DnsSdAdvertiser(const DnsSdAdvertiser&) = delete;
    DnsSdAdvertiser& operator=(const DnsSdAdvertiser&) = delete;

    /// Registers the service. Bonjour may rename the instance on collision;
    /// the registered name is available in registeredName() once the
    /// callback has run. On macOS 15+ a Local Network permission denial is
    /// reported as ErrorCode::unsupported.
    core::Result<void> publish(const core::platform::ServiceRecord& record) override;
    void unpublish() override;
    [[nodiscard]] std::optional<core::net::NativeHandle> pollHandle() const override;
    void processEvents() override;

    [[nodiscard]] const std::string& registeredName() const noexcept { return m_registeredName; }
    [[nodiscard]] const std::string& lastError() const noexcept { return m_lastError; }

private:
    static void onRegister(void* ref, unsigned int flags, int error, const char* name, const char* type,
                           const char* domain, void* context);

    void* m_ref = nullptr; // DNSServiceRef
    std::string m_registeredName;
    std::string m_lastError;
};

} // namespace tapein::platform
