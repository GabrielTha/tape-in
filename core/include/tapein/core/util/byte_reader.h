// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace tapein::core {

/// Bounds-checked big-endian reader over a byte span. Every read either
/// returns a value and advances, or returns nullopt and leaves the position
/// untouched. This is the only way network bytes are read in the core.
class ByteReader {
public:
    explicit ByteReader(std::span<const uint8_t> data) noexcept : m_data(data) {}

    [[nodiscard]] size_t remaining() const noexcept { return m_data.size() - m_pos; }
    [[nodiscard]] size_t position() const noexcept { return m_pos; }

    bool skip(size_t n) noexcept
    {
        if (n > remaining()) {
            return false;
        }
        m_pos += n;
        return true;
    }

    std::optional<uint8_t> u8() noexcept
    {
        if (remaining() < 1) {
            return std::nullopt;
        }
        return m_data[m_pos++];
    }

    std::optional<uint16_t> u16be() noexcept
    {
        if (remaining() < 2) {
            return std::nullopt;
        }
        const auto v = static_cast<uint16_t>((static_cast<uint16_t>(m_data[m_pos]) << 8) | m_data[m_pos + 1]);
        m_pos += 2;
        return v;
    }

    std::optional<uint32_t> u32be() noexcept
    {
        if (remaining() < 4) {
            return std::nullopt;
        }
        uint32_t v = 0;
        for (size_t i = 0; i < 4; ++i) {
            v = (v << 8) | m_data[m_pos + i];
        }
        m_pos += 4;
        return v;
    }

    std::optional<uint64_t> u64be() noexcept
    {
        if (remaining() < 8) {
            return std::nullopt;
        }
        uint64_t v = 0;
        for (size_t i = 0; i < 8; ++i) {
            v = (v << 8) | m_data[m_pos + i];
        }
        m_pos += 8;
        return v;
    }

    std::optional<std::span<const uint8_t>> bytes(size_t n) noexcept
    {
        if (n > remaining()) {
            return std::nullopt;
        }
        const auto out = m_data.subspan(m_pos, n);
        m_pos += n;
        return out;
    }

private:
    std::span<const uint8_t> m_data;
    size_t m_pos = 0;
};

} // namespace tapein::core
