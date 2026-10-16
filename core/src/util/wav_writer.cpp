// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/util/wav_writer.h"

#include <array>
#include <cstring>
#include <limits>
#include <utility>

namespace tapein::core::util {

namespace {

constexpr size_t kHeaderBytes = 44;

void putLe16(uint8_t* p, uint16_t v) noexcept
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}

void putLe32(uint8_t* p, uint32_t v) noexcept
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

std::array<uint8_t, kHeaderBytes> makeHeader(uint32_t sampleRate, uint16_t channels, uint32_t dataBytes) noexcept
{
    std::array<uint8_t, kHeaderBytes> h{};
    const uint16_t blockAlign = static_cast<uint16_t>(channels * 2);
    std::memcpy(&h[0], "RIFF", 4);
    putLe32(&h[4], 36 + dataBytes);
    std::memcpy(&h[8], "WAVE", 4);
    std::memcpy(&h[12], "fmt ", 4);
    putLe32(&h[16], 16);
    putLe16(&h[20], 1); // PCM
    putLe16(&h[22], channels);
    putLe32(&h[24], sampleRate);
    putLe32(&h[28], sampleRate * blockAlign);
    putLe16(&h[32], blockAlign);
    putLe16(&h[34], 16);
    std::memcpy(&h[36], "data", 4);
    putLe32(&h[40], dataBytes);
    return h;
}

} // namespace

Result<WavWriter> WavWriter::open(const std::filesystem::path& path, uint32_t sampleRate, uint16_t channels)
{
    if (channels == 0 || sampleRate == 0) {
        return makeError(ErrorCode::invalidArgument, "invalid WAV format");
    }
#ifdef _WIN32
    std::FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0) {
        f = nullptr;
    }
#else
    std::FILE* f = std::fopen(path.c_str(), "wb");
#endif
    if (f == nullptr) {
        return makeError(ErrorCode::ioError, "cannot open WAV file for writing: " + path.string());
    }
    WavWriter w(f, sampleRate, channels);
    if (auto r = w.writeHeader(); !r.ok()) {
        return r.error();
    }
    return w;
}

WavWriter::WavWriter(std::FILE* file, uint32_t sampleRate, uint16_t channels) noexcept
    : m_file(file), m_sampleRate(sampleRate), m_channels(channels)
{
}

WavWriter::WavWriter(WavWriter&& other) noexcept
    : m_file(std::exchange(other.m_file, nullptr))
    , m_sampleRate(other.m_sampleRate)
    , m_channels(other.m_channels)
    , m_frames(other.m_frames)
{
}

WavWriter& WavWriter::operator=(WavWriter&& other) noexcept
{
    if (this != &other) {
        (void)close();
        m_file = std::exchange(other.m_file, nullptr);
        m_sampleRate = other.m_sampleRate;
        m_channels = other.m_channels;
        m_frames = other.m_frames;
    }
    return *this;
}

WavWriter::~WavWriter()
{
    (void)close();
}

Result<void> WavWriter::writeHeader()
{
    const uint64_t dataBytes64 = m_frames * m_channels * 2;
    const uint32_t dataBytes =
        dataBytes64 > std::numeric_limits<uint32_t>::max() ? std::numeric_limits<uint32_t>::max() : static_cast<uint32_t>(dataBytes64);
    const auto header = makeHeader(m_sampleRate, m_channels, dataBytes);
    if (std::fwrite(header.data(), 1, header.size(), m_file) != header.size()) {
        return makeError(ErrorCode::ioError, "WAV header write failed");
    }
    return {};
}

Result<void> WavWriter::write(std::span<const int16_t> interleaved)
{
    if (m_file == nullptr) {
        return makeError(ErrorCode::invalidState, "WAV file is closed");
    }
    if (interleaved.empty()) {
        return {};
    }
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    for (const int16_t s : interleaved) {
        const uint16_t u = static_cast<uint16_t>(s);
        const uint8_t le[2] = {static_cast<uint8_t>(u), static_cast<uint8_t>(u >> 8)};
        if (std::fwrite(le, 1, 2, m_file) != 2) {
            return makeError(ErrorCode::ioError, "WAV write failed");
        }
    }
#else
    if (std::fwrite(interleaved.data(), sizeof(int16_t), interleaved.size(), m_file) != interleaved.size()) {
        return makeError(ErrorCode::ioError, "WAV write failed");
    }
#endif
    m_frames += interleaved.size() / m_channels;
    return {};
}

Result<void> WavWriter::close()
{
    if (m_file == nullptr) {
        return {};
    }
    Result<void> result;
    if (std::fseek(m_file, 0, SEEK_SET) != 0) {
        result = makeError(ErrorCode::ioError, "WAV seek failed");
    } else {
        result = writeHeader();
    }
    if (std::fclose(m_file) != 0 && result.ok()) {
        result = makeError(ErrorCode::ioError, "WAV close failed");
    }
    m_file = nullptr;
    return result;
}

} // namespace tapein::core::util
