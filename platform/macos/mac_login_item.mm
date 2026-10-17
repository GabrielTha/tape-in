// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/platform/mac_login_item.h"

#import <Foundation/Foundation.h>
#import <ServiceManagement/ServiceManagement.h>

namespace tapein::platform {

using Status = core::platform::ILoginItem::Status;

core::Result<void> MacLoginItem::setEnabled(bool enabled)
{
    if (@available(macOS 13.0, *)) {
        SMAppService* service = [SMAppService mainAppService];
        NSError* error = nil;
        const BOOL ok = enabled ? [service registerAndReturnError:&error]
                                : [service unregisterAndReturnError:&error];
        if (!ok) {
            const std::string message = error != nil ? std::string(error.localizedDescription.UTF8String)
                                                      : "SMAppService call failed";
            // No bundle / not a real app: treat as unsupported rather than an error to report.
            return core::makeError(core::ErrorCode::unsupported, message);
        }
        return core::Result<void>{};
    }
    return core::makeError(core::ErrorCode::unsupported, "start at login needs macOS 13 or newer");
}

Status MacLoginItem::status() const
{
    if (@available(macOS 13.0, *)) {
        switch ([[SMAppService mainAppService] status]) {
        case SMAppServiceStatusNotRegistered:
            return Status::notRegistered;
        case SMAppServiceStatusEnabled:
            return Status::enabled;
        case SMAppServiceStatusRequiresApproval:
            return Status::requiresApproval;
        case SMAppServiceStatusNotFound:
            return Status::notFound;
        default:
            return Status::unavailable;
        }
    }
    return Status::unavailable;
}

void MacLoginItem::openSystemSettings()
{
    if (@available(macOS 13.0, *)) {
        [SMAppService openSystemSettingsLoginItems];
    }
}

} // namespace tapein::platform
