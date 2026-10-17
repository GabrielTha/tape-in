// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "AudioOutput.h"

namespace tapein::app {

AudioOutput::AudioOutput(core::dsp::Mixer& mixer) : m_mixer(mixer) {}

AudioOutput::~AudioOutput()
{
    stop();
}

void AudioOutput::start(const juce::XmlElement* savedState)
{
    // 0 inputs, 2 outputs. A saved state restores the chosen device.
    const juce::String error = m_deviceManager.initialise(0, 2, savedState, true);
    if (error.isNotEmpty()) {
        juce::Logger::writeToLog("audio device init: " + error);
    }
    m_deviceManager.addAudioCallback(this);
    m_deviceManager.addChangeListener(this);
    if (onDeviceChanged) {
        onDeviceChanged();
    }
}

void AudioOutput::stop()
{
    m_deviceManager.removeChangeListener(this);
    m_deviceManager.removeAudioCallback(this);
    m_deviceManager.closeAudioDevice();
}

juce::StringArray AudioOutput::outputDeviceNames() const
{
    juce::StringArray names;
    if (auto* type = m_deviceManager.getCurrentDeviceTypeObject()) {
        names = type->getDeviceNames(false);
    }
    return names;
}

juce::String AudioOutput::currentOutputName() const
{
    if (auto* device = m_deviceManager.getCurrentAudioDevice()) {
        return device->getName();
    }
    return {};
}

void AudioOutput::selectOutput(const juce::String& name)
{
    juce::AudioDeviceManager::AudioDeviceSetup setup = m_deviceManager.getAudioDeviceSetup();
    setup.outputDeviceName = name;
    // Keep whatever rate the device prefers; do not force one.
    const juce::String error = m_deviceManager.setAudioDeviceSetup(setup, true);
    if (error.isNotEmpty()) {
        juce::Logger::writeToLog("audio output select: " + error);
    }
    if (onDeviceChanged) {
        onDeviceChanged();
    }
}

double AudioOutput::currentSampleRate() const
{
    return m_sampleRate.load();
}

std::unique_ptr<juce::XmlElement> AudioOutput::deviceState() const
{
    return m_deviceManager.createStateXml();
}

void AudioOutput::audioDeviceIOCallbackWithContext(const float* const*, int, float* const* outputChannelData,
                                                   int numOutputChannels, int numSamples,
                                                   const juce::AudioIODeviceCallbackContext&)
{
    m_mixer.render(outputChannelData, static_cast<uint32_t>(numOutputChannels), static_cast<uint32_t>(numSamples),
                   m_sampleRate.load());
}

void AudioOutput::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    m_sampleRate.store(device->getCurrentSampleRate());
    juce::Logger::writeToLog("audio device: " + device->getName() + " @ " + juce::String(device->getCurrentSampleRate()) + " Hz, outs " + juce::String(device->getActiveOutputChannels().countNumberOfSetBits()));
    m_mixer.setAudioActive(true);
    if (onActiveChanged) {
        onActiveChanged(true);
    }
    if (onDeviceChanged) {
        onDeviceChanged();
    }
}

void AudioOutput::audioDeviceStopped()
{
    m_mixer.setAudioActive(false);
    if (onActiveChanged) {
        onActiveChanged(false);
    }
}

void AudioOutput::changeListenerCallback(juce::ChangeBroadcaster*)
{
    if (onDeviceChanged) {
        onDeviceChanged();
    }
}

} // namespace tapein::app
