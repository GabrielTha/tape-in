// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/crypto/digest_auth.h"

#include <catch2/catch_test_macros.hpp>

using namespace tapein::core::crypto;

TEST_CASE("computeDigestResponse matches the RFC 2617 MD5 vector")
{
    // RFC 2617 §3.5 principals, unqualified MD5 (no qop):
    //   HA1=939e7578ed9e3c518a452acee763bce9, golden computed with an
    //   independent MD5.
    const auto r = computeDigestResponse("Mufasa", "testrealm@host.com", "Circle Of Life",
                                         "dcd98b7102dd2f0e8b11d0f600bfb0c093", "GET", "/dir/index.html");
    CHECK(r == "670fd8c2df070c60b045671b8b24ff02");
}

TEST_CASE("parseDigestAuthorization reads every field from a realistic header")
{
    const auto c = parseDigestAuthorization(
        R"(Digest username="iTunes", realm="Tape In", nonce="abc123", uri="rtsp://host/stream", response="deadbeef")");
    REQUIRE(c.has_value());
    CHECK(c->username == "iTunes");
    CHECK(c->realm == "Tape In");
    CHECK(c->nonce == "abc123");
    CHECK(c->uri == "rtsp://host/stream");
    CHECK(c->response == "deadbeef");
}

TEST_CASE("parseDigestAuthorization tolerates spacing, order and the bare scheme")
{
    // Scheme in any case, no space after commas, reordered keys.
    const auto c = parseDigestAuthorization("digest realm=\"r\",nonce=\"n\",response=\"x\",username=\"u\",uri=\"/p\"");
    REQUIRE(c.has_value());
    CHECK(c->username == "u");
    CHECK(c->uri == "/p");
    CHECK(c->response == "x");
}

TEST_CASE("parseDigestAuthorization keeps the last value for a duplicated key")
{
    const auto c = parseDigestAuthorization(R"(Digest response="first", response="second")");
    REQUIRE(c.has_value());
    CHECK(c->response == "second");
}

TEST_CASE("parseDigestAuthorization rejects malformed input")
{
    CHECK_FALSE(parseDigestAuthorization("").has_value());
    CHECK_FALSE(parseDigestAuthorization("Basic dXNlcjpwYXNz").has_value());    // wrong scheme
    CHECK_FALSE(parseDigestAuthorization(R"(Digest username="unterminated)").has_value());
    CHECK_FALSE(parseDigestAuthorization("Digest justatoken").has_value());     // no '='
}

TEST_CASE("verifyDigest accepts a correct credential and rejects tampering")
{
    DigestCredentials cred;
    cred.username = "iTunes";
    cred.realm = "Tape In";
    cred.nonce = "n0nce";
    cred.uri = "rtsp://host/stream";
    cred.response = computeDigestResponse(cred.username, cred.realm, "hunter2", cred.nonce, "ANNOUNCE", cred.uri);

    CHECK(verifyDigest(cred, "hunter2", "ANNOUNCE", "Tape In", "n0nce"));
    CHECK_FALSE(verifyDigest(cred, "wrong", "ANNOUNCE", "Tape In", "n0nce"));       // wrong password
    CHECK_FALSE(verifyDigest(cred, "hunter2", "ANNOUNCE", "Other", "n0nce"));       // wrong realm
    CHECK_FALSE(verifyDigest(cred, "hunter2", "ANNOUNCE", "Tape In", "stale"));     // wrong nonce
    CHECK_FALSE(verifyDigest(cred, "hunter2", "RECORD", "Tape In", "n0nce"));       // wrong method
}

TEST_CASE("generateNonce returns 32 hex chars and varies")
{
    const auto a = generateNonce();
    const auto b = generateNonce();
    CHECK(a.size() == 32);
    CHECK(a.find_first_not_of("0123456789abcdef") == std::string::npos);
    CHECK(a != b);
}
