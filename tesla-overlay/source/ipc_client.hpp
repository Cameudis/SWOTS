// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <switch.h>

#include "ipc_protocol.hpp"
#include "settings_format.hpp"

namespace swots::ipc {

inline constexpr Result ResultVersionMismatch =
    MAKERESULT(Module_HomebrewAbi, 200);
inline constexpr Result ResultWorkerUnavailable =
    MAKERESULT(Module_HomebrewAbi, 201);

class ClientWorker {
public:
    Result start();
    bool shutdown();
    Result connect(u64 timeoutNs);
    Result claimAndSuspend(u64 ownerEpoch, u64 sequence, u64 timeoutNs);
    Result setConfig(u64 ownerEpoch, u64 revision,
                     const SettingsWire &settings, u64 timeoutNs);
    Result resume(u64 ownerEpoch, u64 sequence, u64 revision,
                  const SettingsWire &settings, u64 timeoutNs);
    Result requestStop(u64 ownerEpoch, u64 sequence, u64 timeoutNs);
    void invalidate();

private:
    enum class Operation : u8 {
        None,
        Connect,
        Claim,
        SetConfig,
        Resume,
        Stop,
        Shutdown,
    };

    struct Request {
        Operation operation = Operation::None;
        u64 deadlineTick = 0;
        OwnerCommand owner{};
        ConfigCommand config{};
        ResumeCommand resume{};
    };

    static void threadEntry(void *argument);
    void threadLoop();
    Result submit(const Request &request, u64 timeoutNs);
    Result execute(const Request &request);
    Result connectService(u64 deadlineTick);
    Result getStatus(StatusWire *status);
    Result waitForApplied(const Request &request);
    void closeService();

    Mutex m_mutex = 0;
    Mutex m_submitMutex = 0;
    UEvent m_requestEvent{};
    UEvent m_completionEvent{};
    Thread m_thread{};
    bool m_threadCreated = false;
    bool m_valid = false;
    bool m_requestPending = false;
    Request m_request{};
    Result m_result = ResultWorkerUnavailable;
    Service m_service{};
    Event m_changed{INVALID_HANDLE, INVALID_HANDLE, false};
};

SettingsWire toWire(const settings::Values &values);

} // namespace swots::ipc
