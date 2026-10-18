// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors
//
// macOS Dock-icon reopen glue: JUCE's NSApplicationDelegate does not forward
// applicationShouldHandleReopen:, so a Dock-icon click with no visible window
// is otherwise ignored. installDockReopenHandler wires it to a C++ callback.

#pragma once

#include <functional>

namespace tapein::app {

/// Routes a Dock-icon click (when the app has no visible windows) to the given
/// handler, so a regular Dock app can reopen its window. macOS only; a no-op if
/// no application delegate is installed yet. Call once, after the app's windows
/// exist.
void installDockReopenHandler(std::function<void()> handler);

} // namespace tapein::app
