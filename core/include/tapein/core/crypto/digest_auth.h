// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace tapein::core::crypto {

/// The fields a sender presents in an `Authorization: Digest ...` header
/// (RFC 2617), as RAOP uses them. Only the fields we need to verify are kept.
struct DigestCredentials {
    std::string username;
    std::string realm;
    std::string nonce;
    std::string uri;
    std::string response; ///< lowercase hex MD5
};

/// Parses an `Authorization` header value. `header` may include the leading
/// `Digest ` scheme token (case-insensitive) or be the scheme's parameter
/// list alone. Every byte is hostile: the parser never allocates unboundedly,
/// never recurses, and returns nullopt on anything it cannot parse cleanly
/// (missing scheme, unterminated quote, a parameter with no `=`). A duplicate
/// key keeps the last occurrence. Unknown keys are ignored.
[[nodiscard]] std::optional<DigestCredentials> parseDigestAuthorization(std::string_view header);

/// RFC 2617 "MD5" (unqualified) response:
///   HA1 = MD5(username ":" realm ":" password)
///   HA2 = MD5(method ":" uri)
///   response = MD5(HA1 ":" nonce ":" HA2)
/// Returned as lowercase hex. Pure and deterministic.
[[nodiscard]] std::string computeDigestResponse(std::string_view username, std::string_view realm,
                                                std::string_view password, std::string_view nonce,
                                                std::string_view method, std::string_view uri);

/// True when `cred` authenticates `method` against `password`: the realm and
/// nonce must equal the ones we issued, and the response must match
/// computeDigestResponse for the credential's own username/uri. The final
/// comparison is constant-time.
[[nodiscard]] bool verifyDigest(const DigestCredentials& cred, std::string_view password, std::string_view method,
                                std::string_view expectedRealm, std::string_view expectedNonce);

/// A fresh challenge nonce: 16 random bytes as lowercase hex. Not a secret;
/// it only needs to be unique per challenge to deter trivial replay.
[[nodiscard]] std::string generateNonce();

} // namespace tapein::core::crypto
