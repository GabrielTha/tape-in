// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/platform/login_item.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>

namespace tapein::app {

/// The menu-bar (status) icon and its menu. Left click toggles the window;
/// right click (or click) opens a menu with show / start-at-login / about /
/// quit.
class TrayIcon final : public juce::SystemTrayIconComponent {
public:
    explicit TrayIcon(core::platform::ILoginItem& loginItem);

    std::function<void()> onToggleWindow;
    std::function<void()> onAbout;
    std::function<void()> onQuit;
    std::function<void(bool)> onStartAtLoginChanged; ///< persists the choice
    std::function<bool()> isIgnoreSenderVolume;
    std::function<void(bool)> onIgnoreSenderVolume;

    void mouseDown(const juce::MouseEvent& e) override;

private:
    static juce::Image makeIcon();
    void showMenu();

    core::platform::ILoginItem& m_loginItem;
};

} // namespace tapein::app
