// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/net/reactor.h"

#include <algorithm>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#endif

namespace tapein::core::net {

namespace {

#ifdef _WIN32
using PollFd = WSAPOLLFD;
int doPoll(PollFd* fds, size_t n, int timeoutMs)
{
    return WSAPoll(fds, static_cast<ULONG>(n), timeoutMs);
}
#else
using PollFd = pollfd;
int doPoll(PollFd* fds, size_t n, int timeoutMs)
{
    return ::poll(fds, static_cast<nfds_t>(n), timeoutMs);
}
#endif

} // namespace

Reactor::Reactor()
{
#ifdef _WIN32
    Endpoint local;
    local.address = {127, 0, 0, 1};
    auto s = UdpSocket::bind(local);
    if (s.ok()) {
        m_wakeEndpoint = s.value().localEndpoint().value();
        m_wakeRead = s.value().handle();
        m_wakeWrite = m_wakeRead;
        // Keep the socket alive for the reactor's lifetime by releasing ownership from the wrapper.
        new UdpSocket(std::move(s.value())); // NOLINT: freed in ~Reactor via closesocket
    }
#else
    int fds[2] = {-1, -1};
    if (::pipe(fds) == 0) {
        for (const int fd : fds) {
            const int flags = ::fcntl(fd, F_GETFL, 0);
            ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
            ::fcntl(fd, F_SETFD, FD_CLOEXEC);
        }
        m_wakeRead = fds[0];
        m_wakeWrite = fds[1];
    }
#endif
}

Reactor::~Reactor()
{
#ifdef _WIN32
    if (m_wakeRead != kInvalidHandle) {
        closesocket(static_cast<SOCKET>(m_wakeRead));
    }
#else
    if (m_wakeRead != kInvalidHandle) {
        ::close(m_wakeRead);
    }
    if (m_wakeWrite != kInvalidHandle) {
        ::close(m_wakeWrite);
    }
#endif
}

void Reactor::add(NativeHandle handle, Callback onReadable)
{
    remove(handle);
    m_handlers.push_back(Handler{handle, std::move(onReadable)});
}

void Reactor::remove(NativeHandle handle)
{
    std::erase_if(m_handlers, [handle](const Handler& h) { return h.handle == handle; });
}

Reactor::TimerId Reactor::addTimer(std::chrono::milliseconds after, Callback fn, bool repeat)
{
    const TimerId id = m_nextTimerId++;
    m_timers.push_back(Timer{id, std::chrono::steady_clock::now() + after, after, repeat, std::move(fn)});
    return id;
}

void Reactor::cancelTimer(TimerId id)
{
    std::erase_if(m_timers, [id](const Timer& t) { return t.id == id; });
}

void Reactor::post(Callback fn)
{
    {
        const std::lock_guard<std::mutex> lock(m_postMutex);
        m_posted.push_back(std::move(fn));
    }
    wake();
}

void Reactor::wake()
{
    if (m_wakeWrite == kInvalidHandle) {
        return;
    }
    const char byte = 1;
#ifdef _WIN32
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(m_wakeEndpoint.port);
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::sendto(static_cast<SOCKET>(m_wakeWrite), &byte, 1, 0, reinterpret_cast<const sockaddr*>(&to), sizeof to);
#else
    (void)!::write(m_wakeWrite, &byte, 1);
#endif
}

void Reactor::drainPosted()
{
    std::vector<Callback> batch;
    {
        const std::lock_guard<std::mutex> lock(m_postMutex);
        batch.swap(m_posted);
    }
    for (auto& fn : batch) {
        fn();
    }
}

void Reactor::runOnce(std::chrono::milliseconds maxWait)
{
    const auto now = std::chrono::steady_clock::now();
    auto timeout = maxWait;
    for (const auto& t : m_timers) {
        const auto until = std::chrono::duration_cast<std::chrono::milliseconds>(t.due - now);
        timeout = std::min(timeout, std::max(until, std::chrono::milliseconds{0}));
    }

    std::vector<PollFd> fds;
    fds.reserve(m_handlers.size() + 1);
    if (m_wakeRead != kInvalidHandle) {
        PollFd p{};
        p.fd = static_cast<decltype(p.fd)>(m_wakeRead);
        p.events = POLLIN;
        fds.push_back(p);
    }
    for (const auto& h : m_handlers) {
        PollFd p{};
        p.fd = static_cast<decltype(p.fd)>(h.handle);
        p.events = POLLIN;
        fds.push_back(p);
    }

    if (fds.empty()) {
        // Nothing to wait on: sleep the timeout so callers do not spin.
        std::this_thread::sleep_for(timeout);
    } else {
        doPoll(fds.data(), fds.size(), static_cast<int>(timeout.count()));
    }

    // Wake channel: drain it.
    size_t index = 0;
    if (m_wakeRead != kInvalidHandle) {
        if (fds[0].revents != 0) {
            char buf[64];
#ifdef _WIN32
            ::recv(static_cast<SOCKET>(m_wakeRead), buf, sizeof buf, 0);
#else
            while (::read(m_wakeRead, buf, sizeof buf) > 0) {
            }
#endif
        }
        index = 1;
    }

    // Readable handles. Callbacks may add/remove handlers, so look each one
    // up again and copy its function before calling.
    std::vector<NativeHandle> ready;
    for (size_t i = index; i < fds.size(); ++i) {
        if (fds[i].revents != 0) {
            ready.push_back(static_cast<NativeHandle>(fds[i].fd));
        }
    }
    for (const NativeHandle h : ready) {
        const auto it = std::find_if(m_handlers.begin(), m_handlers.end(), [h](const Handler& x) { return x.handle == h; });
        if (it == m_handlers.end()) {
            continue;
        }
        Callback fn = it->fn;
        fn();
    }

    // Timers: collect due ids first; a callback may cancel or add timers.
    const auto fireTime = std::chrono::steady_clock::now();
    std::vector<TimerId> due;
    for (const auto& t : m_timers) {
        if (t.due <= fireTime) {
            due.push_back(t.id);
        }
    }
    for (const TimerId id : due) {
        const auto it = std::find_if(m_timers.begin(), m_timers.end(), [id](const Timer& t) { return t.id == id; });
        if (it == m_timers.end()) {
            continue;
        }
        Callback fn = it->fn;
        if (it->repeat) {
            it->due = fireTime + it->interval;
        } else {
            m_timers.erase(it);
        }
        fn();
    }

    drainPosted();
}

void Reactor::run()
{
    m_stop = false;
    while (!m_stop) {
        runOnce(std::chrono::milliseconds{250});
    }
}

void Reactor::stop()
{
    m_stop = true;
    wake();
}

} // namespace tapein::core::net
