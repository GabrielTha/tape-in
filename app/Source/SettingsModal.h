// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace tapein::app {

/// The settings modal from the Config artboard: a scrim over the window and a
/// neumorphic card with the destination name, "Restrict access" + password,
/// "Exclusive solo", the audio-device picker, and the (inert) Output 1/2
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
    };

    SettingsModal();
    ~SettingsModal() override;

    void setValues(const Values& v);
    void setAudioDevices(const juce::StringArray& names);

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

    juce::TextEditor m_name;
    std::unique_ptr<CheckBox> m_restrict;
    juce::TextEditor m_password;
    std::unique_ptr<CheckBox> m_exclusive;
    std::unique_ptr<CheckBox> m_ask;
    std::unique_ptr<Dropdown> m_folder;
    std::unique_ptr<juce::FileChooser> m_chooser;
    juce::String m_folderPath;
    std::unique_ptr<Dropdown> m_audio;
    std::unique_ptr<Dropdown> m_out1;
    std::unique_ptr<Dropdown> m_out2;
    juce::TextButton m_save{"Save"};

    juce::StringArray m_audioDevices;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsModal)
};

} // namespace tapein::app
