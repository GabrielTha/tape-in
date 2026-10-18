// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "DockReopen.h"

#import <AppKit/AppKit.h>
#import <objc/runtime.h>

namespace tapein::app {
namespace {
std::function<void()> g_reopenHandler;
} // namespace

void installDockReopenHandler(std::function<void()> handler)
{
    g_reopenHandler = std::move(handler);

    id delegate = [NSApp delegate];
    if (delegate == nil) {
        return;
    }

    // JUCE owns the application delegate; add the reopen selector to its class
    // so AppKit calls us when the user clicks the Dock icon or app icon and no
    // window is showing. Returning YES lets AppKit perform its default handling
    // too (which is harmless here).
    IMP imp = imp_implementationWithBlock(^BOOL(id, NSApplication*, BOOL hasVisibleWindows) {
        if (!hasVisibleWindows && g_reopenHandler) {
            g_reopenHandler();
        }
        return YES;
    });

    const SEL sel = @selector(applicationShouldHandleReopen:hasVisibleWindows:);
    Class cls = object_getClass(delegate);
    if (!class_addMethod(cls, sel, imp, "c@:@c")) {
        class_replaceMethod(cls, sel, imp, "c@:@c");
    }
}

} // namespace tapein::app
