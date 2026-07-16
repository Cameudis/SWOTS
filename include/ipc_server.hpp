// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <switch.h>

#include "ipc_state.hpp"

namespace swots::ipc {

class RendererServer {
public:
    Result start();
    void stop();

    RenderWork work();
    Result waitForWork(u64 timeoutNs);
    void publishSuspended(const RenderWork &work, u64 revision,
                          bool configValid);
    bool tryPublishRunning(const RenderWork &work);
    void publishStopped();
    void publishFault(Result result);

private:
    static void controlThreadEntry(void *argument);
    void controlLoop();
    void disconnectSession(unsigned slot);
    void signalStateChangedLocked();

    struct Session {
        Handle handle = INVALID_HANDLE;
        Event changed{INVALID_HANDLE, INVALID_HANDLE, false};
        u32 generation = 0;
        u64 acceptedTick = 0;
    };

    Mutex m_mutex = 0;
    StateMachine m_state{};
    UEvent m_controlChanged{};
    Event m_renderChanged{INVALID_HANDLE, INVALID_HANDLE, false};
    Session m_sessions[2]{};
    Handle m_port = INVALID_HANDLE;
    Thread m_controlThread{};
    bool m_threadCreated = false;
    bool m_stopControl = false;
    bool m_resourcesSafe = false;
    u64 m_startupDeadline = 0;
    u64 m_orphanDeadline = 0;
    u64 m_stopDeadline = 0;
};

} // namespace swots::ipc
