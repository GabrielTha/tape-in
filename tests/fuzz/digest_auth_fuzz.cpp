// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "fuzz_main.h"

#include "tapein/core/crypto/digest_auth.h"

#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    using namespace tapein::core::crypto;
    const std::string_view header(reinterpret_cast<const char*>(data), size);
    // The whole input is a hostile Authorization header.
    const auto cred = parseDigestAuthorization(header);
    // Whatever it parsed (or the raw bytes, split in two) must never crash the
    // responder either.
    if (cred) {
        (void)computeDigestResponse(cred->username, cred->realm, "pw", cred->nonce, "ANNOUNCE", cred->uri);
        (void)verifyDigest(*cred, "pw", "ANNOUNCE", cred->realm, cred->nonce);
    }
    const auto half = header.substr(0, header.size() / 2);
    (void)computeDigestResponse(half, half, half, half, "GET", half);
    return 0;
}
