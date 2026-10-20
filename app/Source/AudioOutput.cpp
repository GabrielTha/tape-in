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
    // A new device gets the default 1/2 pair; its channel map is its own.
    setup.useDefaultOutputChannels = true;
    m_chanL.store(0, std::memory_order_relaxed);
    m_chanR.store(1, std::memory_order_relaxed);
    // Keep whatever rate the device prefers; do not force one.
    const juce::String error = m_deviceManager.setAudioDeviceSetup(setup, true);
    if (error.isNotEmpty()) {
        juce::Logger::writeToLog("audio output select: " + error);
    }
    if (onDeviceChanged) {
        onDeviceChanged();
    }
}

juce::StringArray AudioOutput::outputChannelNames() const
{
    if (auto* device = m_deviceManager.getCurrentAudioDevice()) {
        return device->getOutputChannelNames();
    }
    return {};
}

std::pair<int, int> AudioOutput::outputChannelPair() const
{
    return {m_chanL.load(std::memory_order_relaxed), m_chanR.load(std::memory_order_relaxed)};
}

void AudioOutput::selectOutputChannels(int left, int right)
{
    const int count = outputChannelNames().size();
    if (count > 0) {
        left = juce::jlimit(0, count - 1, left);
        right = juce::jlimit(0, count - 1, right);
    }
    m_chanL.store(left, std::memory_order_relaxed);
    m_chanR.store(right, std::memory_order_relaxed);

    juce::AudioDeviceManager::AudioDeviceSetup setup = m_deviceManager.getAudioDeviceSetup();
    setup.useDefaultOutputChannels = false;
    setup.outputChannels.clear();
    setup.outputChannels.setBit(left);
    setup.outputChannels.setBit(right);
    const juce::String error = m_deviceManager.setAudioDeviceSetup(setup, true);
    if (error.isNotEmpty()) {
        juce::Logger::writeToLog("audio output channels: " + error);
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
    // JUCE hands the active channels compacted in ascending device order, so
    // a pair routed L>R (say 4/3) arrives swapped; un-swap for the mixer.
    if (numOutputChannels == 2
        && m_chanL.load(std::memory_order_relaxed) > m_chanR.load(std::memory_order_relaxed)) {
        float* swapped[2] = {outputChannelData[1], outputChannelData[0]};
        m_mixer.render(swapped, 2, static_cast<uint32_t>(numSamples), m_sampleRate.load());
        return;
    }
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
