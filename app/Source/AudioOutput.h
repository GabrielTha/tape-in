// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/dsp/mixer.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <functional>

namespace tapein::app {

/// Owns the output audio device and drives the engine's mixer from its
/// callback. Output only (0 inputs). Never changes the device sample rate.
class AudioOutput final : public juce::AudioIODeviceCallback, private juce::ChangeListener {
public:
    explicit AudioOutput(core::dsp::Mixer& mixer);
    ~AudioOutput() override;

    /// Restores a saved device state (or opens the default) and starts.
    void start(const juce::XmlElement* savedState);
    void stop();

    [[nodiscard]] juce::StringArray outputDeviceNames() const;
    [[nodiscard]] juce::String currentOutputName() const;
    void selectOutput(const juce::String& name);

    /// Channel names of the current device, in device order.
    [[nodiscard]] juce::StringArray outputChannelNames() const;
    /// Device channel indices the mix L/R play on (equal means mono).
    [[nodiscard]] std::pair<int, int> outputChannelPair() const;
    /// Routes the mix to a pair of device channels (clamped to the device).
    void selectOutputChannels(int left, int right);
    [[nodiscard]] double currentSampleRate() const;
    [[nodiscard]] std::unique_ptr<juce::XmlElement> deviceState() const;

    /// Mutes the whole output (the master strip's M). The callback ramps the
    /// gain over a few ms so toggling never clicks.
    void setMasterMuted(bool muted);
    [[nodiscard]] bool masterMuted() const;

    /// Called (on the message thread) whenever the device or its rate changes.
    std::function<void()> onDeviceChanged;
    /// Reports whether audio is currently running, for the mixer's garbage collector.
    std::function<void(bool)> onActiveChanged;

    // juce::AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                          float* const* outputChannelData, int numOutputChannels, int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    void changeListenerCallback(juce::ChangeBroadcaster*) override;

private:
    /// Ramps toward the mute target and scales the rendered block (audio thread).
    void applyMasterGain(float* const* channels, int numChannels, int numSamples);

    core::dsp::Mixer& m_mixer;
    juce::AudioDeviceManager m_deviceManager;
    std::atomic<double> m_sampleRate{44100.0};
    std::atomic<int> m_chanL{0}; ///< device channel playing the mix left
    std::atomic<int> m_chanR{1}; ///< device channel playing the mix right
    std::atomic<float> m_masterTarget{1.0f}; ///< 0 muted, 1 open
    float m_masterGain = 1.0f; ///< ramped gain, audio thread only
};

} // namespace tapein::app
