// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "Settings.h"

#include <random>

namespace tapein::app {

Settings::Settings()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "Tape In";
    options.filenameSuffix = "xml";
    options.folderName = "Tape In";
    options.osxLibrarySubFolder = "Application Support";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    m_properties.setStorageParameters(options);
}

juce::String Settings::receiverName() const
{
    return file().getValue("receiverName", "Tape In");
}

void Settings::setReceiverName(const juce::String& name)
{
    file().setValue("receiverName", name);
    file().saveIfNeeded();
}

core::crypto::HardwareId Settings::hardwareId()
{
    core::crypto::HardwareId id{};
    const juce::String stored = file().getValue("hardwareId", "");
    if (stored.length() == 12) {
        for (int i = 0; i < 6; ++i) {
            id[static_cast<size_t>(i)] = static_cast<uint8_t>(stored.substring(i * 2, i * 2 + 2).getHexValue32());
        }
        return id;
    }
    std::random_device rd;
    std::uniform_int_distribution<int> byte(0, 255);
    for (auto& b : id) {
        b = static_cast<uint8_t>(byte(rd));
    }
    id[0] = static_cast<uint8_t>((id[0] & 0xFC) | 0x02); // locally administered, unicast
    juce::String hex;
    for (const auto b : id) {
        hex += juce::String::toHexString(b).paddedLeft('0', 2);
    }
    file().setValue("hardwareId", hex.toUpperCase());
    file().saveIfNeeded();
    return id;
}

core::session::BufferMode Settings::bufferMode() const
{
    const int v = file().getIntValue("bufferMode", 0);
    switch (v) {
    case 1:
        return core::session::BufferMode::shortBuffer;
    case 2:
        return core::session::BufferMode::monitoring;
    default:
        return core::session::BufferMode::senderSync;
    }
}

void Settings::setBufferMode(core::session::BufferMode mode)
{
    file().setValue("bufferMode", static_cast<int>(mode));
    file().saveIfNeeded();
}

bool Settings::ignoreSenderVolume() const
{
    return file().getBoolValue("ignoreSenderVolume", false);
}

void Settings::setIgnoreSenderVolume(bool ignore)
{
    file().setValue("ignoreSenderVolume", ignore);
    file().saveIfNeeded();
}

bool Settings::requirePassword() const
{
    return file().getBoolValue("requirePassword", false);
}

void Settings::setRequirePassword(bool require)
{
    file().setValue("requirePassword", require);
    file().saveIfNeeded();
}

juce::String Settings::password() const
{
    return file().getValue("password", "");
}

void Settings::setPassword(const juce::String& password)
{
    file().setValue("password", password);
    file().saveIfNeeded();
}

bool Settings::exclusiveSolo() const
{
    return file().getBoolValue("exclusiveSolo", false);
}

void Settings::setExclusiveSolo(bool exclusive)
{
    file().setValue("exclusiveSolo", exclusive);
    file().saveIfNeeded();
}

bool Settings::startAtLogin() const
{
    return file().getBoolValue("startAtLogin", false);
}

void Settings::setStartAtLogin(bool enabled)
{
    file().setValue("startAtLogin", enabled);
    file().saveIfNeeded();
}

std::unique_ptr<juce::XmlElement> Settings::audioDeviceState() const
{
    return file().getXmlValue("audioDeviceState");
}

void Settings::setAudioDeviceState(const juce::XmlElement& state)
{
    file().setValue("audioDeviceState", &state);
    file().saveIfNeeded();
}

juce::Rectangle<int> Settings::windowBounds() const
{
    const juce::String s = file().getValue("windowBounds", "");
    return s.isEmpty() ? juce::Rectangle<int>() : juce::Rectangle<int>::fromString(s);
}

void Settings::setWindowBounds(juce::Rectangle<int> bounds)
{
    file().setValue("windowBounds", bounds.toString());
    file().saveIfNeeded();
}

} // namespace tapein::app
