// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/crypto/raop_key.h"
#include "tapein/core/util/result.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace tapein::core::crypto {

/// The 6-byte ID that forms the "<12 HEX>@Name" Bonjour prefix. It must be
/// the same bytes used here, otherwise senders reject the response.
using HardwareId = std::array<uint8_t, 6>;

/// Computes the Apple-Response header value for an Apple-Challenge.
///
/// The signed blob is challenge ‖ localAddress ‖ hardwareId, zero-padded to
/// at least 32 bytes, signed with raw RSA PKCS#1 v1.5 and base64-encoded
/// without trailing '='. challengeBase64 may lack its '=' padding (iOS
/// strips it); it must decode to at most 16 bytes. localAddress is the 4
/// (IPv4) or 16 (IPv6) bytes of the RTSP socket's local address.
Result<std::string> appleChallengeResponse(RaopRsaKey& key, std::string_view challengeBase64,
                                           std::span<const uint8_t> localAddress, const HardwareId& hardwareId);

} // namespace tapein::core::crypto
