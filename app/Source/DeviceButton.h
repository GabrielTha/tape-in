// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace tapein::app {

/// The centred output-device pill from the artboards: a raised neumorphic
/// button showing a speaker icon, the current device name and a chevron.
/// Clicking drops a styled menu of the available outputs with a tick on the
/// current one (the DeviceMenu artboard). Selecting one calls `onSelect`.
class DeviceButton final : public juce::Button {
public:
    DeviceButton();

    void setDevices(const juce::StringArray& names, const juce::String& current);
    std::function<void(const juce::String&)> onSelect;

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
    void showMenu();

    juce::StringArray m_names;
    juce::String m_current;
    bool m_open = false;
};

} // namespace tapein::app
