// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/rtsp/rtsp_message.h"
#include "tapein/core/util/result.h"

#include <cstdint>
#include <span>
#include <vector>

namespace tapein::core::rtsp {

/// Incremental RTSP request parser for one TCP connection.
///
/// Bytes may arrive in any chunking. feed() returns every request completed
/// so far, in order. On malformed input it returns an Error; the caller must
/// close the connection and discard the parser (its state is undefined).
class Parser {
public:
    Result<std::vector<Request>> feed(std::span<const uint8_t> bytes);

private:
    enum class State { readingHeaders, readingBody };

    Result<bool> tryParseHeaders();
    bool tryTakeBody(std::vector<Request>& out);

    State m_state = State::readingHeaders;
    std::vector<uint8_t> m_buffer;
    size_t m_scanFrom = 0;
    Request m_current;
    size_t m_bodyExpected = 0;
};

} // namespace tapein::core::rtsp
