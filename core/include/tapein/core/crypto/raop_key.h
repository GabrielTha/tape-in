// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/util/result.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace tapein::core::crypto {

/// The RSA private key every AirPlay-1 receiver uses to answer
/// Apple-Challenge and unwrap the session AES key. Provenance and the
/// project's decision to embed it: docs/LEGAL.md. This is the only place
/// the key material lives; swapping it for a user-supplied file means
/// changing this one function.
std::string_view raopPrivateKeyPem() noexcept;

/// Parsed RAOP RSA key. Load once, share between sessions. All methods are
/// const and safe to call from several threads only if the underlying
/// library is (Mbed TLS RSA contexts are not thread-safe for blinding), so
/// Tape In calls them from the network thread only.
class RaopRsaKey {
public:
    ~RaopRsaKey();
    RaopRsaKey(const RaopRsaKey&) = delete;
    RaopRsaKey& operator=(const RaopRsaKey&) = delete;

    /// Parses the embedded PEM. Fails only if the build is broken.
    static Result<std::shared_ptr<RaopRsaKey>> load();
    static Result<std::shared_ptr<RaopRsaKey>> loadFromPem(std::string_view pem);

    [[nodiscard]] size_t modulusBytes() const noexcept; // 256 for this key

    /// RSA PKCS#1 v1.5 signature of the raw input (no hash, no DigestInfo),
    /// as RAOP's Apple-Response requires. Input must be shorter than
    /// modulusBytes() - 11.
    Result<std::vector<uint8_t>> signRawPkcs1v15(std::span<const uint8_t> input);

    /// RSA-OAEP (SHA-1, MGF1-SHA-1, empty label) decryption. Input must be
    /// exactly modulusBytes() long.
    Result<std::vector<uint8_t>> decryptOaepSha1(std::span<const uint8_t> cipher);

private:
    RaopRsaKey();
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace tapein::core::crypto
