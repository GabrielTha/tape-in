// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/crypto/apple_challenge.h"

#include "tapein/core/util/base64.h"

#include <vector>

namespace tapein::core::crypto {

namespace {
constexpr size_t kMaxChallengeBytes = 16;
constexpr size_t kMinSignedBytes = 32;
} // namespace

Result<std::string> appleChallengeResponse(RaopRsaKey& key, std::string_view challengeBase64,
                                           std::span<const uint8_t> localAddress, const HardwareId& hardwareId)
{
    const auto challenge = base64Decode(challengeBase64);
    if (!challenge) {
        return makeError(ErrorCode::malformedInput, "Apple-Challenge is not base64");
    }
    if (challenge->empty() || challenge->size() > kMaxChallengeBytes) {
        return makeError(ErrorCode::malformedInput, "Apple-Challenge has an invalid length");
    }
    if (localAddress.size() != 4 && localAddress.size() != 16) {
        return makeError(ErrorCode::invalidArgument, "local address must be 4 or 16 bytes");
    }

    std::vector<uint8_t> blob;
    blob.reserve(kMinSignedBytes + kMaxChallengeBytes);
    blob.insert(blob.end(), challenge->begin(), challenge->end());
    blob.insert(blob.end(), localAddress.begin(), localAddress.end());
    blob.insert(blob.end(), hardwareId.begin(), hardwareId.end());
    if (blob.size() < kMinSignedBytes) {
        blob.resize(kMinSignedBytes, 0);
    }

    auto signature = key.signRawPkcs1v15(blob);
    if (!signature.ok()) {
        return signature.error();
    }
    return base64Encode(signature.value(), false);
}

} // namespace tapein::core::crypto
