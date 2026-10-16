// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/net/socket.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace tapein::core::net {

/// Single-threaded poll() loop: readable callbacks per socket handle,
/// one-shot and repeating timers, and a thread-safe post() that wakes the
/// loop. Everything except post() and stop() must be called from the
/// thread that runs the reactor.
class Reactor {
public:
    using Callback = std::function<void()>;
    using TimerId = uint64_t;

    Reactor();
    ~Reactor();
    Reactor(const Reactor&) = delete;
    Reactor& operator=(const Reactor&) = delete;

    void add(NativeHandle handle, Callback onReadable);
    void remove(NativeHandle handle);

    TimerId addTimer(std::chrono::milliseconds after, Callback fn, bool repeat);
    void cancelTimer(TimerId id);

    /// Thread-safe. Runs fn on the reactor thread during the next iteration.
    void post(Callback fn);

    /// One poll iteration: waits at most maxWait (less if a timer is due),
    /// dispatches readable callbacks, due timers and posted functions.
    void runOnce(std::chrono::milliseconds maxWait);
    /// Loops runOnce() until stop() is called.
    void run();
    /// Thread-safe.
    void stop();

private:
    struct Handler {
        NativeHandle handle;
        Callback fn;
    };
    struct Timer {
        TimerId id;
        std::chrono::steady_clock::time_point due;
        std::chrono::milliseconds interval;
        bool repeat;
        Callback fn;
    };

    void drainPosted();
    void wake();

    std::vector<Handler> m_handlers;
    std::vector<Timer> m_timers;
    TimerId m_nextTimerId = 1;

    std::mutex m_postMutex;
    std::vector<Callback> m_posted;
    std::atomic<bool> m_stop{false};

    // Wake-up channel: a pipe on POSIX, a loopback UDP socket on Windows.
    NativeHandle m_wakeRead = kInvalidHandle;
    NativeHandle m_wakeWrite = kInvalidHandle;
    Endpoint m_wakeEndpoint;
};

} // namespace tapein::core::net
