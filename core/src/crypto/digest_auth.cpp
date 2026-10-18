// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/crypto/digest_auth.h"

#include <mbedtls/md.h>

#include <array>
#include <cctype>
#include <cstdint>
#include <random>
#include <string>

namespace tapein::core::crypto {

namespace {

std::string toHex(const uint8_t* data, size_t len)
{
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(kDigits[data[i] >> 4]);
        out.push_back(kDigits[data[i] & 0x0F]);
    }
    return out;
}

// MD5 of the concatenation of the given parts, as lowercase hex.
std::string md5Hex(std::initializer_list<std::string_view> parts)
{
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_MD5);
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    std::array<uint8_t, 16> digest{};
    // setup(..., 0) = no HMAC. On the off chance MD5 is unavailable the calls
    // fail and we return an all-zero hex, which never matches a real response.
    if (info != nullptr && mbedtls_md_setup(&ctx, info, 0) == 0 && mbedtls_md_starts(&ctx) == 0) {
        for (const auto part : parts) {
            mbedtls_md_update(&ctx, reinterpret_cast<const uint8_t*>(part.data()), part.size());
        }
        mbedtls_md_finish(&ctx, digest.data());
    }
    mbedtls_md_free(&ctx);
    return toHex(digest.data(), digest.size());
}

std::string_view trim(std::string_view s) noexcept
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
        s.remove_suffix(1);
    }
    return s;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

void assign(DigestCredentials& cred, std::string_view key, std::string_view value)
{
    if (equalsIgnoreCase(key, "username")) {
        cred.username = std::string(value);
    } else if (equalsIgnoreCase(key, "realm")) {
        cred.realm = std::string(value);
    } else if (equalsIgnoreCase(key, "nonce")) {
        cred.nonce = std::string(value);
    } else if (equalsIgnoreCase(key, "uri")) {
        cred.uri = std::string(value);
    } else if (equalsIgnoreCase(key, "response")) {
        cred.response = std::string(value);
    }
    // Unknown keys (algorithm, qop, cnonce, nc, opaque, ...) are ignored.
}

} // namespace

std::optional<DigestCredentials> parseDigestAuthorization(std::string_view header)
{
    header = trim(header);
    // Require the "Digest" scheme token followed by whitespace.
    const size_t sp = header.find(' ');
    if (sp == std::string_view::npos || !equalsIgnoreCase(header.substr(0, sp), "Digest")) {
        return std::nullopt;
    }
    std::string_view rest = trim(header.substr(sp + 1));
    if (rest.empty()) {
        return std::nullopt;
    }

    DigestCredentials cred;
    size_t i = 0;
    const size_t n = rest.size();
    while (i < n) {
        // key
        const size_t keyStart = i;
        while (i < n && rest[i] != '=' && rest[i] != ',') {
            ++i;
        }
        if (i >= n || rest[i] != '=') {
            return std::nullopt; // a parameter with no value
        }
        const std::string_view key = trim(rest.substr(keyStart, i - keyStart));
        ++i; // consume '='

        // value: either "quoted" (no escapes in RAOP practice) or a bare token.
        std::string_view value;
        if (i < n && rest[i] == '"') {
            ++i; // opening quote
            const size_t valStart = i;
            while (i < n && rest[i] != '"') {
                ++i;
            }
            if (i >= n) {
                return std::nullopt; // unterminated quote
            }
            value = rest.substr(valStart, i - valStart);
            ++i; // closing quote
        } else {
            const size_t valStart = i;
            while (i < n && rest[i] != ',') {
                ++i;
            }
            value = trim(rest.substr(valStart, i - valStart));
        }

        if (key.empty()) {
            return std::nullopt;
        }
        assign(cred, key, value);

        // separator
        while (i < n && (rest[i] == ' ' || rest[i] == '\t')) {
            ++i;
        }
        if (i < n) {
            if (rest[i] != ',') {
                return std::nullopt;
            }
            ++i; // consume ','
            while (i < n && (rest[i] == ' ' || rest[i] == '\t')) {
                ++i;
            }
        }
    }
    return cred;
}

std::string computeDigestResponse(std::string_view username, std::string_view realm, std::string_view password,
                                  std::string_view nonce, std::string_view method, std::string_view uri)
{
    const std::string ha1 = md5Hex({username, ":", realm, ":", password});
    const std::string ha2 = md5Hex({method, ":", uri});
    return md5Hex({ha1, ":", nonce, ":", ha2});
}

bool verifyDigest(const DigestCredentials& cred, std::string_view password, std::string_view method,
                  std::string_view expectedRealm, std::string_view expectedNonce)
{
    if (cred.realm != expectedRealm || cred.nonce != expectedNonce) {
        return false;
    }
    const std::string expected =
        computeDigestResponse(cred.username, expectedRealm, password, expectedNonce, method, cred.uri);
    if (expected.size() != cred.response.size()) {
        return false;
    }
    // Constant-time compare.
    unsigned char diff = 0;
    for (size_t i = 0; i < expected.size(); ++i) {
        diff |= static_cast<unsigned char>(expected[i]) ^ static_cast<unsigned char>(cred.response[i]);
    }
    return diff == 0;
}

std::string generateNonce()
{
    std::random_device rd;
    std::array<uint8_t, 16> bytes{};
    for (auto& b : bytes) {
        b = static_cast<uint8_t>(rd());
    }
    return toHex(bytes.data(), bytes.size());
}

} // namespace tapein::core::crypto
