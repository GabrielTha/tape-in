// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/rtsp/rtsp_message.h"

#include <algorithm>
#include <cctype>

namespace tapein::core::rtsp {

namespace {

bool equalsIgnoreCase(std::string_view a, std::string_view b) noexcept
{
    return a.size() == b.size()
           && std::equal(a.begin(), a.end(), b.begin(), [](unsigned char x, unsigned char y) {
                  return std::tolower(x) == std::tolower(y);
              });
}

void append(std::vector<uint8_t>& out, std::string_view s)
{
    out.insert(out.end(), s.begin(), s.end());
}

} // namespace

void Headers::set(std::string name, std::string value)
{
    for (auto& item : m_items) {
        if (equalsIgnoreCase(item.first, name)) {
            item.second = std::move(value);
            return;
        }
    }
    m_items.emplace_back(std::move(name), std::move(value));
}

std::optional<std::string_view> Headers::get(std::string_view name) const noexcept
{
    for (const auto& item : m_items) {
        if (equalsIgnoreCase(item.first, name)) {
            return item.second;
        }
    }
    return std::nullopt;
}

std::vector<uint8_t> Response::serialize() const
{
    std::vector<uint8_t> out;
    out.reserve(128 + body.size());
    append(out, "RTSP/1.0 ");
    append(out, std::to_string(status));
    append(out, " ");
    append(out, reason);
    append(out, "\r\n");
    for (const auto& [name, value] : headers.items()) {
        append(out, name);
        append(out, ": ");
        append(out, value);
        append(out, "\r\n");
    }
    append(out, "Content-Length: ");
    append(out, std::to_string(body.size()));
    append(out, "\r\n\r\n");
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

} // namespace tapein::core::rtsp
