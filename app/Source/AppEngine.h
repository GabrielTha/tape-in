// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "AudioOutput.h"
#include "Settings.h"

#include "tapein/core/crypto/raop_key.h"
#include "tapein/core/session/engine.h"
#include "tapein/platform/dns_sd_advertiser.h"

#include <memory>

namespace tapein::app {

/// Builds and owns the core Engine (which runs its own network thread),
/// the service advertiser and the audio output, from the saved settings.
class AppEngine {
public:
    explicit AppEngine(Settings& settings);
    ~AppEngine();

    /// Starts the receiver and the audio device. Returns false and sets
    /// lastError() when the receiver could not start.
    bool start();
    void stop();

    [[nodiscard]] core::session::Engine* engine() noexcept { return m_engine.get(); }
    [[nodiscard]] AudioOutput& audio() noexcept { return *m_audio; }
    [[nodiscard]] const juce::String& lastError() const noexcept { return m_lastError; }
    [[nodiscard]] bool started() const noexcept { return m_started; }

private:
    Settings& m_settings;
    tapein::platform::DnsSdAdvertiser m_advertiser;
    std::shared_ptr<core::crypto::RaopRsaKey> m_key;
    std::unique_ptr<core::session::Engine> m_engine;
    std::unique_ptr<AudioOutput> m_audio;
    juce::String m_lastError;
    bool m_started = false;
};

} // namespace tapein::app
