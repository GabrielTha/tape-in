// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/crypto/aes_cbc.h"

#include <mbedtls/aes.h>

#include <algorithm>

namespace tapein::core::crypto {

struct AesCbcDecryptor::Impl {
    mbedtls_aes_context ctx{};
    Impl() { mbedtls_aes_init(&ctx); }
    ~Impl() { mbedtls_aes_free(&ctx); }
};

AesCbcDecryptor::AesCbcDecryptor(std::span<const uint8_t, 16> key, std::span<const uint8_t, 16> iv)
    : m_impl(std::make_unique<Impl>())
{
    std::copy(iv.begin(), iv.end(), m_iv.begin());
    mbedtls_aes_setkey_dec(&m_impl->ctx, key.data(), 128);
}

AesCbcDecryptor::~AesCbcDecryptor() = default;

bool AesCbcDecryptor::decryptPacketInPlace(std::span<uint8_t> payload) noexcept
{
    const size_t encryptedLength = payload.size() & ~static_cast<size_t>(15);
    if (encryptedLength == 0) {
        return false;
    }
    std::array<uint8_t, 16> iv = m_iv; // reset per packet; mbedtls advances it in place
    return mbedtls_aes_crypt_cbc(&m_impl->ctx, MBEDTLS_AES_DECRYPT, encryptedLength, iv.data(), payload.data(),
                                 payload.data())
           == 0;
}

} // namespace tapein::core::crypto
