// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/crypto/raop_key.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>

#include <string>

namespace tapein::core::crypto {

namespace {

// Private key of the AirPort Express, the key all AirPlay-1 receivers use.
// See docs/LEGAL.md for provenance and the decision to embed it.
constexpr const char kRaopPrivateKeyPem[] =
    "-----BEGIN RSA PRIVATE KEY-----\n"
    "MIIEpQIBAAKCAQEA59dE8qLieItsH1WgjrcFRKj6eUWqi+bGLOX1HL3U3GhC/j0Qg90u3sG/1CUt\n"
    "wC5vOYvfDmFI6oSFXi5ELabWJmT2dKHzBJKa3k9ok+8t9ucRqMd6DZHJ2YCCLlDRKSKv6kDqnw4U\n"
    "wPdpOMXziC/AMj3Z/lUVX1G7WSHCAWKf1zNS1eLvqr+boEjXuBOitnZ/bDzPHrTOZz0Dew0uowxf\n"
    "/+sG+NCK3eQJVxqcaJ/vEHKIVd2M+5qL71yJQ+87X6oV3eaYvt3zWZYD6z5vYTcrtij2VZ9Zmni/\n"
    "UAaHqn9JdsBWLUEpVviYnhimNVvYFZeCXg/IdTQ+x4IRdiXNv5hEewIDAQABAoIBAQDl8Axy9XfW\n"
    "BLmkzkEiqoSwF0PsmVrPzH9KsnwLGH+QZlvjWd8SWYGN7u1507HvhF5N3drJoVU3O14nDY4TFQAa\n"
    "LlJ9VM35AApXaLyY1ERrN7u9ALKd2LUwYhM7Km539O4yUFYikE2nIPscEsA5ltpxOgUGCY7b7ez5\n"
    "NtD6nL1ZKauw7aNXmVAvmJTcuPxWmoktF3gDJKK2wxZuNGcJE0uFQEG4Z3BrWP7yoNuSK3dii2jm\n"
    "lpPHr0O/KnPQtzI3eguhe0TwUem/eYSdyzMyVx/YpwkzwtYL3sR5k0o9rKQLtvLzfAqdBxBurciz\n"
    "aaA/L0HIgAmOit1GJA2saMxTVPNhAoGBAPfgv1oeZxgxmotiCcMXFEQEWflzhWYTsXrhUIuz5jFu\n"
    "a39GLS99ZEErhLdrwj8rDDViRVJ5skOp9zFvlYAHs0xh92ji1E7V/ysnKBfsMrPkk5KSKPrnjndM\n"
    "oPdevWnVkgJ5jxFuNgxkOLMuG9i53B4yMvDTCRiIPMQ++N2iLDaRAoGBAO9v//mU8eVkQaoANf0Z\n"
    "oMjW8CN4xwWA2cSEIHkd9AfFkftuv8oyLDCG3ZAf0vrhrrtkrfa7ef+AUb69DNggq4mHQAYBp7L+\n"
    "k5DKzJrKuO0r+R0YbY9pZD1+/g9dVt91d6LQNepUE/yY2PP5CNoFmjedpLHMOPFdVgqDzDFxU8hL\n"
    "AoGBANDrr7xAJbqBjHVwIzQ4To9pb4BNeqDndk5Qe7fT3+/H1njGaC0/rXE0Qb7q5ySgnsCb3DvA\n"
    "cJyRM9SJ7OKlGt0FMSdJD5KG0XPIpAVNwgpXXH5MDJg09KHeh0kXo+QA6viFBi21y340NonnEfdf\n"
    "54PX4ZGS/Xac1UK+pLkBB+zRAoGAf0AY3H3qKS2lMEI4bzEFoHeK3G895pDaK3TFBVmD7fV0Zhov\n"
    "17fegFPMwOII8MisYm9ZfT2Z0s5Ro3s5rkt+nvLAdfC/PYPKzTLalpGSwomSNYJcB9HNMlmhkGzc\n"
    "1JnLYT4iyUyx6pcZBmCd8bD0iwY/FzcgNDaUmbX9+XDvRA0CgYEAkE7pIPlE71qvfJQgoA9em0gI\n"
    "LAuE4Pu13aKiJnfft7hIjbK+5kyb3TysZvoyDnb3HOKvInK7vXbKuU4ISgxB2bB3HcYzQMGsz1qJ\n"
    "2gG0N5hvJpzwwhbhXqFKA4zaaSrw622wDniAK5MlIE0tIAKKP4yxNGjoD2QYjhBGuhvkWKY=\n"
    "-----END RSA PRIVATE KEY-----\n";

std::string mbedError(int code)
{
    char buf[128];
    mbedtls_strerror(code, buf, sizeof buf);
    return std::string(buf);
}

} // namespace

std::string_view raopPrivateKeyPem() noexcept
{
    return kRaopPrivateKeyPem;
}

struct RaopRsaKey::Impl {
    mbedtls_pk_context pk{};
    mbedtls_entropy_context entropy{};
    mbedtls_ctr_drbg_context drbg{};
    bool initialised = false;

    Impl()
    {
        mbedtls_pk_init(&pk);
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&drbg);
    }
    ~Impl()
    {
        mbedtls_ctr_drbg_free(&drbg);
        mbedtls_entropy_free(&entropy);
        mbedtls_pk_free(&pk);
    }
    mbedtls_rsa_context* rsa() { return mbedtls_pk_rsa(pk); }
};

RaopRsaKey::RaopRsaKey() : m_impl(std::make_unique<Impl>()) {}
RaopRsaKey::~RaopRsaKey() = default;

Result<std::shared_ptr<RaopRsaKey>> RaopRsaKey::load()
{
    return loadFromPem(raopPrivateKeyPem());
}

Result<std::shared_ptr<RaopRsaKey>> RaopRsaKey::loadFromPem(std::string_view pem)
{
    std::shared_ptr<RaopRsaKey> key(new RaopRsaKey());
    Impl& impl = *key->m_impl;

    static const char kPersonalisation[] = "tapein-raop-key";
    int rc = mbedtls_ctr_drbg_seed(&impl.drbg, mbedtls_entropy_func, &impl.entropy,
                                   reinterpret_cast<const unsigned char*>(kPersonalisation),
                                   sizeof kPersonalisation - 1);
    if (rc != 0) {
        return makeError(ErrorCode::cryptoFailure, "DRBG seed failed: " + mbedError(rc));
    }

    // mbedtls_pk_parse_key wants the terminating NUL included in the length for PEM.
    const std::string pemZ(pem);
    rc = mbedtls_pk_parse_key(&impl.pk, reinterpret_cast<const unsigned char*>(pemZ.c_str()), pemZ.size() + 1,
                              nullptr, 0, mbedtls_ctr_drbg_random, &impl.drbg);
    if (rc != 0) {
        return makeError(ErrorCode::cryptoFailure, "RSA key parse failed: " + mbedError(rc));
    }
    if (mbedtls_pk_get_type(&impl.pk) != MBEDTLS_PK_RSA) {
        return makeError(ErrorCode::cryptoFailure, "RAOP key is not RSA");
    }
    impl.initialised = true;
    return key;
}

size_t RaopRsaKey::modulusBytes() const noexcept
{
    return mbedtls_rsa_get_len(m_impl->rsa());
}

Result<std::vector<uint8_t>> RaopRsaKey::signRawPkcs1v15(std::span<const uint8_t> input)
{
    const size_t n = modulusBytes();
    if (input.empty() || input.size() + 11 > n) {
        return makeError(ErrorCode::invalidArgument, "input too long for raw PKCS#1 v1.5 signature");
    }
    mbedtls_rsa_context* rsa = m_impl->rsa();
    const int rc = mbedtls_rsa_set_padding(rsa, MBEDTLS_RSA_PKCS_V15, MBEDTLS_MD_NONE);
    if (rc != 0) {
        return makeError(ErrorCode::cryptoFailure, "set padding failed: " + mbedError(rc));
    }
    std::vector<uint8_t> sig(n);
    const int src = mbedtls_rsa_pkcs1_sign(rsa, mbedtls_ctr_drbg_random, &m_impl->drbg, MBEDTLS_MD_NONE,
                                           static_cast<unsigned int>(input.size()), input.data(), sig.data());
    if (src != 0) {
        return makeError(ErrorCode::cryptoFailure, "RSA sign failed: " + mbedError(src));
    }
    return sig;
}

Result<std::vector<uint8_t>> RaopRsaKey::decryptOaepSha1(std::span<const uint8_t> cipher)
{
    const size_t n = modulusBytes();
    if (cipher.size() != n) {
        return makeError(ErrorCode::invalidArgument, "RSA-OAEP input must equal the modulus size");
    }
    mbedtls_rsa_context* rsa = m_impl->rsa();
    const int rc = mbedtls_rsa_set_padding(rsa, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA1);
    if (rc != 0) {
        return makeError(ErrorCode::cryptoFailure, "set padding failed: " + mbedError(rc));
    }
    std::vector<uint8_t> out(n);
    size_t outLen = 0;
    const int drc = mbedtls_rsa_rsaes_oaep_decrypt(rsa, mbedtls_ctr_drbg_random, &m_impl->drbg, nullptr, 0, &outLen,
                                                   cipher.data(), out.data(), out.size());
    if (drc != 0) {
        return makeError(ErrorCode::cryptoFailure, "RSA-OAEP decrypt failed: " + mbedError(drc));
    }
    out.resize(outLen);
    return out;
}

} // namespace tapein::core::crypto
