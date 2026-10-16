// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tapein::core::rtsp {

/// Header collection with case-insensitive names and insertion order kept.
class Headers {
public:
    /// Sets a header, replacing an existing one with the same (case-insensitive) name.
    void set(std::string name, std::string value);
    [[nodiscard]] std::optional<std::string_view> get(std::string_view name) const noexcept;
    [[nodiscard]] size_t size() const noexcept { return m_items.size(); }
    [[nodiscard]] const std::vector<std::pair<std::string, std::string>>& items() const noexcept { return m_items; }

private:
    std::vector<std::pair<std::string, std::string>> m_items;
};

struct Request {
    std::string method;
    std::string uri;
    std::string version; // "RTSP/1.0" or "HTTP/1.1"
    Headers headers;
    std::vector<uint8_t> body;
};

struct Response {
    int status = 200;
    std::string reason = "OK";
    Headers headers;
    std::vector<uint8_t> body;

    /// "RTSP/1.0 <status> <reason>\r\n" + headers + "Content-Length: N\r\n" (always) + "\r\n" + body.
    [[nodiscard]] std::vector<uint8_t> serialize() const;
};

inline constexpr size_t kMaxHeaderBlockBytes = 16 * 1024;
inline constexpr size_t kMaxHeaders = 64;
inline constexpr size_t kMaxBodyBytes = 1024u * 1024; // artwork bodies are a few hundred KB

} // namespace tapein::core::rtsp
