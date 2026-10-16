// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/cli/args.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace tapein::cli;

namespace {
Result<Args> parse(std::vector<std::string> argv)
{
    std::vector<const char*> raw;
    raw.push_back("tapein-cli");
    for (const auto& a : argv) {
        raw.push_back(a.c_str());
    }
    return parseArgs(static_cast<int>(raw.size()), raw.data());
}
} // namespace

TEST_CASE("cli parses defaults")
{
    const auto r = parse({});
    REQUIRE(r.ok());
    const Args& a = r.value();
    REQUIRE(a.name == "Tape In");
    REQUIRE(a.outDir == ".");
    REQUIRE(a.port == 0);
    REQUIRE(a.maxSessions == 100);
    REQUIRE(a.txtEt == "0,1");
    REQUIRE(a.answerAppleChallenge);
    REQUIRE_FALSE(a.verbose);
    REQUIRE(a.captureDir.empty());
    REQUIRE_FALSE(a.showHelp);
    // Derived hardware id: stable for the name, locally-administered, unicast.
    REQUIRE(a.hardwareId == hardwareIdForName("Tape In"));
    REQUIRE((a.hardwareId[0] & 0x02) == 0x02);
    REQUIRE((a.hardwareId[0] & 0x01) == 0x00);
    REQUIRE(hardwareIdForName("Tape In") != hardwareIdForName("Other"));
}

TEST_CASE("cli parses every flag")
{
    const auto r = parse({"--name", "Studio", "--out-dir", "/tmp/x", "--port", "5123", "--hwid", "0a0B0c0D0e0F", "--txt-et",
                          "0", "--no-apple-response", "--verbose", "--max-sessions", "3", "--capture", "/tmp/cap"});
    REQUIRE(r.ok());
    const Args& a = r.value();
    REQUIRE(a.name == "Studio");
    REQUIRE(a.outDir == "/tmp/x");
    REQUIRE(a.port == 5123);
    REQUIRE(a.hardwareId == tapein::core::crypto::HardwareId{0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F});
    REQUIRE(a.txtEt == "0");
    REQUIRE_FALSE(a.answerAppleChallenge);
    REQUIRE(a.verbose);
    REQUIRE(a.maxSessions == 3);
    REQUIRE(a.captureDir == "/tmp/cap");
}

TEST_CASE("cli rejects bad hwid, bad port and unknown flags")
{
    REQUIRE_FALSE(parse({"--hwid", "zz"}).ok());
    REQUIRE_FALSE(parse({"--hwid", "0A0B0C0D0E"}).ok());
    REQUIRE_FALSE(parse({"--port", "70000"}).ok());
    REQUIRE_FALSE(parse({"--port", "abc"}).ok());
    REQUIRE_FALSE(parse({"--bogus"}).ok());
    REQUIRE_FALSE(parse({"--name"}).ok()); // missing value
}

TEST_CASE("cli help flag")
{
    const auto r = parse({"--help"});
    REQUIRE(r.ok());
    REQUIRE(r.value().showHelp);
    REQUIRE(usageText().find("--name") != std::string::npos);
}
