// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/crypto/apple_challenge.h"
#include "tapein/core/session/engine.h"

#include <juce_graphics/juce_graphics.h>
#include <juce_data_structures/juce_data_structures.h>

namespace tapein::app {

/// Persisted settings in ~/Library/Application Support/Tape In/settings.xml.
/// Each setter writes through immediately.
class Settings {
public:
    Settings();

    [[nodiscard]] juce::String receiverName() const;
    void setReceiverName(const juce::String& name);

    /// Six-byte RAOP hardware id, generated once and kept stable. Byte 0 is
    /// forced to a locally-administered unicast value.
    [[nodiscard]] core::crypto::HardwareId hardwareId();

    [[nodiscard]] core::session::BufferMode bufferMode() const;
    void setBufferMode(core::session::BufferMode mode);

    [[nodiscard]] bool ignoreSenderVolume() const;
    void setIgnoreSenderVolume(bool ignore);

    /// "Restrict access": require an RTSP password. Stored plaintext, like
    /// the hardware id.
    [[nodiscard]] bool requirePassword() const;
    void setRequirePassword(bool require);
    [[nodiscard]] juce::String password() const;
    void setPassword(const juce::String& password);

    [[nodiscard]] bool exclusiveSolo() const;
    void setExclusiveSolo(bool exclusive);

    /// Ask to allow each new device before its audio plays.
    [[nodiscard]] bool askBeforeAccept() const;
    void setAskBeforeAccept(bool ask);

    /// Where recorded samples are written. Defaults to ~/Music/Tape In Samples
    /// (created on demand).
    [[nodiscard]] juce::File samplesFolder() const;
    void setSamplesFolder(const juce::File& folder);

    [[nodiscard]] bool startAtLogin() const;
    void setStartAtLogin(bool enabled);

    [[nodiscard]] std::unique_ptr<juce::XmlElement> audioDeviceState() const;
    void setAudioDeviceState(const juce::XmlElement& state);

    /// Device channel indices the mix L/R play on (Output routing).
    [[nodiscard]] int outputChannelLeft() const;
    [[nodiscard]] int outputChannelRight() const;
    void setOutputChannels(int left, int right);

    [[nodiscard]] juce::Rectangle<int> windowBounds() const;
    void setWindowBounds(juce::Rectangle<int> bounds);

private:
    [[nodiscard]] juce::PropertiesFile& file() const { return *m_properties.getUserSettings(); }

    mutable juce::ApplicationProperties m_properties;
};

} // namespace tapein::app
