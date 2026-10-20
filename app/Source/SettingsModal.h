// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

namespace tapein::app {

/// The settings modal from the Config artboard: a scrim over the window and a
/// neumorphic card with the destination name, "Restrict access" + password,
/// "Exclusive solo", the audio-device picker, and the Output 1/2 channel
/// routing. Save applies; the scrim or Close cancels.
class SettingsModal final : public juce::Component {
public:
    struct Values {
        juce::String name;
        bool requirePassword = false;
        juce::String password;
        bool exclusiveSolo = false;
        bool askBeforeAccept = false;
        juce::String samplesFolder;
        juce::String audioDevice;
        int bufferIndex = 0; ///< 0 Sync, 1 Short, 2 Monitor
        int outLeft = 0;  ///< device channel playing the mix left
        int outRight = 1; ///< device channel playing the mix right
    };

    SettingsModal();
    ~SettingsModal() override;

    void setValues(const Values& v);
    void setAudioDevices(const juce::StringArray& names);
    /// Channel names of the device the Output 1/2 pickers choose from.
    void setOutputChannelNames(const juce::StringArray& names);

    std::function<void(const Values&)> onApply;
    std::function<void()> onClose;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    class CheckBox;
    class Dropdown;

    [[nodiscard]] juce::Rectangle<int> cardBounds() const;
    [[nodiscard]] Values gather() const;
    void relayout();
    void showChannelMenu(bool left); ///< popup for one Output picker
    void refreshChannelTexts();

    juce::TextEditor m_name;
    std::unique_ptr<CheckBox> m_restrict;
    juce::TextEditor m_password;
    std::unique_ptr<CheckBox> m_exclusive;
    std::unique_ptr<CheckBox> m_ask;
    std::unique_ptr<Dropdown> m_folder;
    std::unique_ptr<juce::FileChooser> m_chooser;
    juce::String m_folderPath;
    std::unique_ptr<Dropdown> m_audio;
    std::array<juce::TextButton, 3> m_buffer{juce::TextButton{"Sync"}, juce::TextButton{"Short"},
                                             juce::TextButton{"Monitor"}};
    int m_bufferIndex = 0;
    std::unique_ptr<Dropdown> m_out1;
    std::unique_ptr<Dropdown> m_out2;
    int m_outLeft = 0;
    int m_outRight = 1;
    juce::TextButton m_save{"Save"};

    juce::StringArray m_audioDevices;
    juce::StringArray m_channelNames;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsModal)
};

} // namespace tapein::app
