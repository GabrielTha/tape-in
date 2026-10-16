// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/platform/dns_sd_advertiser.h"

#include <dns_sd.h>

#include <arpa/inet.h>

#include <string>

namespace tapein::platform {

using core::ErrorCode;
using core::makeError;

namespace {

std::string describe(DNSServiceErrorType err)
{
    switch (err) {
    case kDNSServiceErr_NoError:
        return "no error";
    case kDNSServiceErr_NameConflict:
        return "name conflict";
    case kDNSServiceErr_ServiceNotRunning:
        return "mDNSResponder is not running";
    case kDNSServiceErr_PolicyDenied:
        return "Local Network permission denied";
    case kDNSServiceErr_BadParam:
        return "bad parameter";
    default:
        return "DNSService error " + std::to_string(err);
    }
}

} // namespace

DnsSdAdvertiser::~DnsSdAdvertiser()
{
    unpublish();
}

void DnsSdAdvertiser::onRegister(void* /*ref*/, unsigned int /*flags*/, int error, const char* name, const char* /*type*/,
                                 const char* /*domain*/, void* context)
{
    auto* self = static_cast<DnsSdAdvertiser*>(context);
    if (error == kDNSServiceErr_NoError) {
        self->m_registeredName = name != nullptr ? name : "";
        self->m_lastError.clear();
    } else {
        self->m_lastError = describe(error);
    }
}

core::Result<void> DnsSdAdvertiser::publish(const core::platform::ServiceRecord& record)
{
    unpublish();

    TXTRecordRef txt;
    TXTRecordCreate(&txt, 0, nullptr);
    for (const auto& [key, value] : record.txt) {
        if (value.size() > 255) {
            TXTRecordDeallocate(&txt);
            return makeError(ErrorCode::invalidArgument, "TXT value too long: " + key);
        }
        const DNSServiceErrorType err =
            TXTRecordSetValue(&txt, key.c_str(), static_cast<uint8_t>(value.size()), value.data());
        if (err != kDNSServiceErr_NoError) {
            TXTRecordDeallocate(&txt);
            return makeError(ErrorCode::invalidArgument, "TXT record: " + describe(err));
        }
    }

    DNSServiceRef ref = nullptr;
    const DNSServiceErrorType err = DNSServiceRegister(
        &ref, 0 /* allow auto-rename on collision */, kDNSServiceInterfaceIndexAny, record.instanceName.c_str(),
        record.type.c_str(), nullptr /* default domain */, nullptr /* default host */, htons(record.port),
        TXTRecordGetLength(&txt), TXTRecordGetBytesPtr(&txt),
        reinterpret_cast<DNSServiceRegisterReply>(&DnsSdAdvertiser::onRegister), this);
    TXTRecordDeallocate(&txt);

    if (err != kDNSServiceErr_NoError) {
        m_lastError = describe(err);
        const ErrorCode code = err == kDNSServiceErr_PolicyDenied ? ErrorCode::unsupported : ErrorCode::ioError;
        return makeError(code, "DNSServiceRegister failed: " + m_lastError);
    }
    m_ref = ref;
    return {};
}

void DnsSdAdvertiser::unpublish()
{
    if (m_ref != nullptr) {
        DNSServiceRefDeallocate(static_cast<DNSServiceRef>(m_ref));
        m_ref = nullptr;
    }
    m_registeredName.clear();
}

std::optional<core::net::NativeHandle> DnsSdAdvertiser::pollHandle() const
{
    if (m_ref == nullptr) {
        return std::nullopt;
    }
    const int fd = DNSServiceRefSockFD(static_cast<DNSServiceRef>(m_ref));
    if (fd < 0) {
        return std::nullopt;
    }
    return fd;
}

void DnsSdAdvertiser::processEvents()
{
    if (m_ref == nullptr) {
        return;
    }
    const DNSServiceErrorType err = DNSServiceProcessResult(static_cast<DNSServiceRef>(m_ref));
    if (err != kDNSServiceErr_NoError) {
        m_lastError = describe(err);
        // The connection to mDNSResponder is gone; drop the ref so the
        // reactor stops polling a dead descriptor.
        unpublish();
    }
}

} // namespace tapein::platform
