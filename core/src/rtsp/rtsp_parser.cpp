// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/rtsp/rtsp_parser.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <string_view>

namespace tapein::core::rtsp {

namespace {

constexpr std::string_view kDelimiter = "\r\n\r\n";

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

bool isToken(std::string_view s) noexcept
{
    if (s.empty()) {
        return false;
    }
    return std::all_of(s.begin(), s.end(), [](unsigned char c) { return c > 32 && c < 127; });
}

// Parses "METHOD SP URI SP VERSION" with exactly three tokens.
bool parseRequestLine(std::string_view line, Request& req)
{
    const size_t sp1 = line.find(' ');
    if (sp1 == std::string_view::npos) {
        return false;
    }
    const size_t sp2 = line.find(' ', sp1 + 1);
    if (sp2 == std::string_view::npos) {
        return false;
    }
    const std::string_view method = line.substr(0, sp1);
    const std::string_view uri = line.substr(sp1 + 1, sp2 - sp1 - 1);
    const std::string_view version = line.substr(sp2 + 1);
    if (!isToken(method) || !isToken(uri) || !isToken(version)) {
        return false;
    }
    if (version != "RTSP/1.0" && version != "HTTP/1.1") {
        return false;
    }
    if (!std::all_of(method.begin(), method.end(), [](unsigned char c) { return std::isupper(c) || c == '_'; })) {
        return false;
    }
    req.method = std::string(method);
    req.uri = std::string(uri);
    req.version = std::string(version);
    return true;
}

std::optional<size_t> parseContentLength(std::string_view v)
{
    v = trim(v);
    if (v.empty() || v.size() > 10) {
        return std::nullopt;
    }
    size_t value = 0;
    const auto [ptr, ec] = std::from_chars(v.data(), v.data() + v.size(), value);
    if (ec != std::errc{} || ptr != v.data() + v.size()) {
        return std::nullopt;
    }
    return value;
}

} // namespace

Result<std::vector<Request>> Parser::feed(std::span<const uint8_t> bytes)
{
    m_buffer.insert(m_buffer.end(), bytes.begin(), bytes.end());
    std::vector<Request> out;

    for (;;) {
        if (m_state == State::readingHeaders) {
            const auto progressed = tryParseHeaders();
            if (!progressed.ok()) {
                return progressed.error();
            }
            if (!progressed.value()) {
                return out; // need more bytes
            }
        }
        if (m_state == State::readingBody) {
            if (!tryTakeBody(out)) {
                return out; // need more bytes
            }
        }
    }
}

// Returns true when a header block was consumed (state moved on), false when
// more bytes are needed. Error on malformed or oversized input.
Result<bool> Parser::tryParseHeaders()
{
    const std::string_view view(reinterpret_cast<const char*>(m_buffer.data()), m_buffer.size());
    const size_t searchStart = m_scanFrom >= 3 ? m_scanFrom - 3 : 0;
    const size_t end = view.find(kDelimiter, searchStart);
    if (end == std::string_view::npos) {
        if (m_buffer.size() > kMaxHeaderBlockBytes) {
            return makeError(ErrorCode::tooLarge, "RTSP header block exceeds limit");
        }
        m_scanFrom = m_buffer.size();
        return false;
    }
    if (end + kDelimiter.size() > kMaxHeaderBlockBytes) {
        return makeError(ErrorCode::tooLarge, "RTSP header block exceeds limit");
    }

    const std::string_view block = view.substr(0, end);
    Request req;

    size_t lineStart = 0;
    bool first = true;
    while (lineStart <= block.size()) {
        size_t lineEnd = block.find("\r\n", lineStart);
        if (lineEnd == std::string_view::npos) {
            lineEnd = block.size();
        }
        const std::string_view line = block.substr(lineStart, lineEnd - lineStart);
        if (first) {
            if (!parseRequestLine(line, req)) {
                return makeError(ErrorCode::malformedInput, "bad RTSP request line");
            }
            first = false;
        } else {
            const size_t colon = line.find(':');
            if (colon == std::string_view::npos || colon == 0) {
                return makeError(ErrorCode::malformedInput, "bad RTSP header line");
            }
            const std::string_view name = trim(line.substr(0, colon));
            if (!isToken(name)) {
                return makeError(ErrorCode::malformedInput, "bad RTSP header name");
            }
            if (req.headers.size() >= kMaxHeaders) {
                return makeError(ErrorCode::tooLarge, "too many RTSP headers");
            }
            req.headers.set(std::string(name), std::string(trim(line.substr(colon + 1))));
        }
        if (lineEnd == block.size()) {
            break;
        }
        lineStart = lineEnd + 2;
    }

    size_t bodyLength = 0;
    if (const auto cl = req.headers.get("Content-Length")) {
        const auto parsed = parseContentLength(*cl);
        if (!parsed) {
            return makeError(ErrorCode::malformedInput, "bad Content-Length");
        }
        if (*parsed > kMaxBodyBytes) {
            return makeError(ErrorCode::tooLarge, "Content-Length exceeds limit");
        }
        bodyLength = *parsed;
    }

    m_buffer.erase(m_buffer.begin(), m_buffer.begin() + static_cast<std::ptrdiff_t>(end + kDelimiter.size()));
    m_scanFrom = 0;
    m_current = std::move(req);
    m_bodyExpected = bodyLength;
    m_state = State::readingBody;
    return true;
}

bool Parser::tryTakeBody(std::vector<Request>& out)
{
    if (m_buffer.size() < m_bodyExpected) {
        return false;
    }
    m_current.body.assign(m_buffer.begin(), m_buffer.begin() + static_cast<std::ptrdiff_t>(m_bodyExpected));
    m_buffer.erase(m_buffer.begin(), m_buffer.begin() + static_cast<std::ptrdiff_t>(m_bodyExpected));
    out.push_back(std::move(m_current));
    m_current = Request{};
    m_bodyExpected = 0;
    m_state = State::readingHeaders;
    return true;
}

} // namespace tapein::core::rtsp
