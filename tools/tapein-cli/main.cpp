// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors
//
// tapein-cli: advertises one AirPlay-1 receiver and writes every session it
// receives to a WAV file. Phase-1 tool; no audio device, no resampling.

#include "tapein/cli/args.h"
#include "tapein/cli/wav_sink.h"
#include "tapein/core/crypto/raop_key.h"
#include "tapein/core/net/reactor.h"
#include "tapein/core/session/receiver.h"
#include "tapein/core/version.h"
#include "tapein/platform/null_advertiser.h"
#if defined(TAPEIN_HAS_DNS_SD)
#include "tapein/platform/dns_sd_advertiser.h"
#endif

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <memory>
#include <string>

namespace {

tapein::core::net::Reactor* g_reactor = nullptr;
std::atomic<int> g_signals{0};

void onSignal(int)
{
    if (++g_signals > 2) {
        std::_Exit(130); // third Ctrl-C: give up waiting for a clean stop
    }
    if (g_reactor != nullptr) {
        g_reactor->stop();
    }
}

void logLine(const std::string& message)
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%H:%M:%S", &local);
    std::fprintf(stderr, "%s.%03lld %s\n", stamp, static_cast<long long>(ms), message.c_str());
}

} // namespace

int main(int argc, char** argv)
{
    using namespace tapein;

    auto parsed = cli::parseArgs(argc, argv);
    if (!parsed.ok()) {
        std::fprintf(stderr, "error: %s\n\n%s", parsed.error().message.c_str(), cli::usageText().c_str());
        return 2;
    }
    const cli::Args& args = parsed.value();
    if (args.showHelp) {
        std::fputs(cli::usageText().c_str(), stdout);
        return 0;
    }

    std::error_code ec;
    std::filesystem::create_directories(args.outDir, ec);
    if (ec) {
        std::fprintf(stderr, "error: cannot create output directory %s: %s\n", args.outDir.c_str(), ec.message().c_str());
        return 2;
    }

    auto key = core::crypto::RaopRsaKey::load();
    if (!key.ok()) {
        std::fprintf(stderr, "error: %s\n", key.error().message.c_str());
        return 1;
    }

    core::net::Reactor reactor;
    g_reactor = &reactor;

    std::unique_ptr<core::platform::IServiceAdvertiser> advertiser;
#if defined(TAPEIN_HAS_DNS_SD)
    advertiser = std::make_unique<platform::DnsSdAdvertiser>();
#else
    advertiser = std::make_unique<platform::NullAdvertiser>();
    logLine("warning: no DNS-SD backend on this platform; the receiver is not advertised (use --port and connect manually)");
#endif

    const bool verbose = args.verbose;
    auto log = [verbose](std::string message) {
        if (verbose || message.rfind("conn ", 0) != 0) {
            logLine(message);
        }
    };

    cli::WavSink sink(args.outDir, logLine);

    core::session::ReceiverConfig config;
    config.name = args.name;
    config.hardwareId = args.hardwareId;
    config.bindAddress.port = args.port;
    config.maxSessions = args.maxSessions;
    config.txtEt = args.txtEt;
    config.answerAppleChallenge = args.answerAppleChallenge;
    config.log = log;

    core::session::Receiver receiver(reactor, config, sink, *advertiser, key.value());
    if (auto r = receiver.start(); !r.ok()) {
        std::fprintf(stderr, "error: %s\n", r.error().message.c_str());
        return 1;
    }

    const auto record = receiver.serviceRecord();
    std::printf("tapein-cli %s\n", std::string(core::versionString()).c_str());
    std::printf("receiver: %s\n", record.instanceName.c_str());
    std::printf("port: %u\n", static_cast<unsigned>(receiver.rtspPort()));
    std::printf("output: %s\n", std::filesystem::absolute(args.outDir).string().c_str());
    if (!args.answerAppleChallenge || args.txtEt != "0,1") {
        std::printf("experiment: txt et=%s, apple-response=%s\n", args.txtEt.c_str(),
                    args.answerAppleChallenge ? "on" : "off");
    }
    std::printf("waiting for senders; Ctrl-C to stop\n");
    std::fflush(stdout);

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    reactor.run();

    logLine("stopping");
    receiver.stop();
    g_reactor = nullptr;
    std::printf("wrote %zu file(s)\n", sink.files().size());
    for (const auto& f : sink.files()) {
        std::printf("  %s\n", f.string().c_str());
    }
    return 0;
}
