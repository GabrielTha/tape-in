// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "AppEngine.h"

namespace tapein::app {

AppEngine::AppEngine(Settings& settings) : m_settings(settings) {}

AppEngine::~AppEngine()
{
    stop();
}

bool AppEngine::start()
{
    if (m_started) {
        return true;
    }
    auto key = core::crypto::RaopRsaKey::load();
    if (!key.ok()) {
        m_lastError = juce::String("cannot load the RAOP key: ") + key.error().message;
        return false;
    }
    m_key = key.value();

    core::session::EngineConfig cfg;
    cfg.receiver.name = m_settings.receiverName().toStdString();
    cfg.receiver.hardwareId = m_settings.hardwareId();
    cfg.bufferMode = m_settings.bufferMode();
    cfg.ignoreSenderVolume = m_settings.ignoreSenderVolume();
    cfg.exclusiveSolo = m_settings.exclusiveSolo();
    cfg.receiver.requirePassword = m_settings.requirePassword();
    cfg.receiver.password = m_settings.password().toStdString();
    cfg.receiver.log = [](std::string m) { juce::Logger::writeToLog(m); };

    core::session::Engine::ResolverFactory resolverFactory;
#if defined(TAPEIN_HAS_DNS_SD)
    resolverFactory = [](core::net::Reactor& reactor) -> std::unique_ptr<core::platform::IServiceResolver> {
        return std::make_unique<tapein::platform::DnsSdResolver>(reactor);
    };
#endif
    m_engine = std::make_unique<core::session::Engine>(std::move(cfg), m_advertiser, m_key, nullptr,
                                                       std::move(resolverFactory));
    auto started = m_engine->start();
    if (!started.ok()) {
        m_lastError = juce::String("receiver: ") + started.error().message;
        m_engine.reset();
        return false;
    }

    m_audio = std::make_unique<AudioOutput>(m_engine->mixer());
    const auto state = m_settings.audioDeviceState();
    m_audio->onDeviceChanged = [this] {
        if (auto xml = m_audio->deviceState()) {
            m_settings.setAudioDeviceState(*xml);
        }
    };
    m_audio->start(state.get());
    m_lastError.clear();
    m_started = true;
    return true;
}

void AppEngine::stop()
{
    if (!m_started) {
        return;
    }
    if (m_audio) {
        m_audio->stop();
        m_audio.reset();
    }
    if (m_engine) {
        m_engine->stop();
        m_engine.reset();
    }
    m_started = false;
}

} // namespace tapein::app
