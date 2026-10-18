// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/session/dmap.h"

#include "tapein/core/util/byte_reader.h"
#include "tapein/core/util/text.h"

#include <array>
#include <charconv>

namespace tapein::core::session {

namespace {

constexpr size_t kMaxStringBytes = 256;

bool codeIs(const std::array<uint8_t, 4>& code, const char (&lit)[5])
{
    return code[0] == static_cast<uint8_t>(lit[0]) && code[1] == static_cast<uint8_t>(lit[1])
           && code[2] == static_cast<uint8_t>(lit[2]) && code[3] == static_cast<uint8_t>(lit[3]);
}

std::string_view asView(std::span<const uint8_t> bytes)
{
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// Walks the top-level tags of a DMAP body; recurses into container codes up
// to maxDepth. Every length is bounds-checked by ByteReader.
void scan(std::span<const uint8_t> body, DmapMetadata& out, int depth)
{
    ByteReader reader(body);
    while (reader.remaining() >= 8) {
        const auto codeBytes = reader.bytes(4);
        if (!codeBytes)
            break;
        std::array<uint8_t, 4> code{(*codeBytes)[0], (*codeBytes)[1], (*codeBytes)[2], (*codeBytes)[3]};
        const auto len = reader.u32be();
        if (!len)
            break;
        const auto payload = reader.bytes(*len); // nullopt if it overruns the buffer
        if (!payload)
            break;

        if (codeIs(code, "minm"))
            out.trackName = util::clampUtf8(asView(*payload), kMaxStringBytes);
        else if (codeIs(code, "asar"))
            out.artist = util::clampUtf8(asView(*payload), kMaxStringBytes);
        else if (codeIs(code, "asal"))
            out.album = util::clampUtf8(asView(*payload), kMaxStringBytes);
        else if (depth > 0 && (codeIs(code, "mlit") || codeIs(code, "mlcl") || codeIs(code, "mshl")))
            scan(*payload, out, depth - 1);
    }
}

} // namespace

DmapMetadata parseDmapMetadata(std::span<const uint8_t> body)
{
    DmapMetadata md;
    scan(body, md, 1);
    return md;
}

std::optional<DmapProgress> parseProgress(std::string_view line)
{
    if (line.starts_with("progress:"))
        line.remove_prefix(std::string_view("progress:").size());
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
        line.remove_prefix(1);

    std::array<uint32_t, 3> values{};
    size_t idx = 0;
    const char* p = line.data();
    const char* end = line.data() + line.size();
    while (p != end && idx < 3) {
        uint32_t value = 0;
        const auto result = std::from_chars(p, end, value);
        if (result.ec != std::errc())
            return std::nullopt;
        values[idx++] = value;
        p = result.ptr;
        if (idx < 3) {
            if (p == end || *p != '/')
                return std::nullopt;
            ++p; // skip '/'
        }
    }
    if (idx != 3 || p != end)
        return std::nullopt;

    return DmapProgress{values[0], values[1], values[2]};
}

} // namespace tapein::core::session
