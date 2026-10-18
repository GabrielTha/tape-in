// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/session/dacp_client.h"

#include <catch2/catch_test_macros.hpp>

using tapein::core::session::buildDacpGet;
using tapein::core::session::dacpPath;
using tapein::core::session::dacpVolumePath;
using tapein::core::session::TransportCommand;

TEST_CASE("dacpPath maps transport commands to control paths")
{
    CHECK(dacpPath(TransportCommand::playPause) == "/ctrl-int/1/playpause");
    CHECK(dacpPath(TransportCommand::next) == "/ctrl-int/1/nextitem");
    CHECK(dacpPath(TransportCommand::previous) == "/ctrl-int/1/previtem");
}

TEST_CASE("dacpVolumePath formats and clamps the device volume")
{
    CHECK(dacpVolumePath(-12.0f) == "/ctrl-int/1/setproperty?dmcp.device-volume=-12.0000");
    CHECK(dacpVolumePath(0.0f) == "/ctrl-int/1/setproperty?dmcp.device-volume=0.0000");
    CHECK(dacpVolumePath(5.0f) == "/ctrl-int/1/setproperty?dmcp.device-volume=0.0000");    // clamp high
    CHECK(dacpVolumePath(-99.0f) == "/ctrl-int/1/setproperty?dmcp.device-volume=-30.0000"); // clamp low
}

TEST_CASE("buildDacpGet emits a complete HTTP request")
{
    CHECK(buildDacpGet("/ctrl-int/1/playpause", "192.168.1.5", 3689, "cafebabe")
          == "GET /ctrl-int/1/playpause HTTP/1.1\r\n"
             "Host: 192.168.1.5:3689\r\n"
             "Active-Remote: cafebabe\r\n"
             "Connection: close\r\n\r\n");
}
