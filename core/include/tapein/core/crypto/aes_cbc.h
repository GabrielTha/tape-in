// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>

namespace tapein::core::crypto {

/// AES-128-CBC decryptor for RAOP audio payloads: no padding, the IV is
/// reset to the ANNOUNCE value for every packet, and any trailing bytes
/// that do not fill a block are transmitted in clear and left untouched.
class AesCbcDecryptor {
public:
    AesCbcDecryptor(std::span<const uint8_t, 16> key, std::span<const uint8_t, 16> iv);
    ~AesCbcDecryptor();
    AesCbcDecryptor(const AesCbcDecryptor&) = delete;
    AesCbcDecryptor& operator=(const AesCbcDecryptor&) = delete;

    /// Decrypts the leading multiple-of-16 bytes in place. Returns false if
    /// the payload is shorter than one block. Never allocates.
    bool decryptPacketInPlace(std::span<uint8_t> payload) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::array<uint8_t, 16> m_iv{};
};

} // namespace tapein::core::crypto
