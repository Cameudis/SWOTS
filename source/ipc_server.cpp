// SPDX-License-Identifier: GPL-2.0-or-later

#include "ipc_server.hpp"

#include <algorithm>
#include <cstring>

namespace swots::ipc {
namespace {

constexpr Result protocolResult(StateResult result) {
    return result == StateResult::Success
               ? 0
               : MAKERESULT(Module_HomebrewAbi,
                            100 + static_cast<u32>(result));
}

u64 deadlineAfter(u64 nanoseconds) {
    return armGetSystemTick() + armNsToTicks(nanoseconds);
}

u64 remainingNs(u64 deadline) {
    if (deadline == 0) return UINT64_MAX;
    const u64 now = armGetSystemTick();
    return now >= deadline ? 0 : armTicksToNs(deadline - now);
}

u64 minimumDeadline(u64 a, u64 b) {
    if (a == 0) return b;
    if (b == 0) return a;
    return std::min(a, b);
}

struct ParsedRequest {
    u32 command = 0;
    const void *payload = nullptr;
    size_t payloadSize = 0;
};

bool parseRequest(ParsedRequest *out) {
    const HipcParsedRequest hipc = hipcParseRequest(armGetTls());
    if (hipc.meta.type != CmifCommandType_Request ||
        hipc.meta.num_send_statics != 0 ||
        hipc.meta.num_send_buffers != 0 ||
        hipc.meta.num_recv_buffers != 0 ||
        hipc.meta.num_exch_buffers != 0 ||
        hipc.meta.num_recv_statics != 0 || hipc.meta.send_pid != 0 ||
        hipc.meta.num_copy_handles != 0 || hipc.meta.num_move_handles != 0)
        return false;

    const auto *begin = reinterpret_cast<const u8 *>(hipc.data.data_words);
    const auto *end = begin + hipc.meta.num_data_words * sizeof(u32);
    const auto *header = reinterpret_cast<const CmifInHeader *>(
        cmifGetAlignedDataStart(hipc.data.data_words, armGetTls()));
    if (reinterpret_cast<const u8 *>(header) < begin ||
        reinterpret_cast<const u8 *>(header + 1) > end ||
        header->magic != CMIF_IN_HEADER_MAGIC || header->version != 0 ||
        header->token != 0)
        return false;

    size_t expected = 0;
    switch (static_cast<Command>(header->command_id)) {
        case Command::GetProtocolInfo:
        case Command::AcquireStateChangedEvent:
        case Command::GetStatus:
            break;
        case Command::ClaimAndSuspend:
        case Command::RequestStop:
            expected = sizeof(OwnerCommand);
            break;
        case Command::SetConfig:
            expected = sizeof(ConfigCommand);
            break;
        case Command::Resume:
            expected = sizeof(ResumeCommand);
            break;
        default:
            return false;
    }
    const u32 expectedWords = static_cast<u32>(
        (16 + sizeof(CmifInHeader) + expected + 3) / 4);
    if (hipc.meta.num_data_words != expectedWords) return false;
    if (reinterpret_cast<const u8 *>(header + 1) + expected > end) return false;
    out->command = header->command_id;
    out->payload = header + 1;
    out->payloadSize = expected;
    return true;
}

void makeReply(Result result, const void *data, size_t size,
               Handle copyHandle = INVALID_HANDLE) {
    std::memset(armGetTls(), 0, 0x100);
    const bool hasHandle = copyHandle != INVALID_HANDLE;
    const u32 words = static_cast<u32>((16 + sizeof(CmifOutHeader) + size + 3) / 4);
    HipcRequest response = hipcMakeRequestInline(
        armGetTls(), .type = 0, .num_data_words = words,
        .num_copy_handles = hasHandle ? 1U : 0U);
    if (hasHandle) *response.copy_handles = copyHandle;
    auto *header = reinterpret_cast<CmifOutHeader *>(
        cmifGetAlignedDataStart(response.data_words, armGetTls()));
    *header = {CMIF_OUT_HEADER_MAGIC, 0, result, 0};
    if (size != 0) std::memcpy(header + 1, data, size);
}

} // namespace

Result RendererServer::start() {
    mutexInit(&m_mutex);
    // Desired state is level-triggered, so coalescing multiple control changes
    // into one auto-cleared wakeup is safe and avoids a suspended busy loop.
    ueventCreate(&m_controlChanged, true);
    Result rc = eventCreate(&m_renderChanged, false);
    if (R_FAILED(rc)) return rc;
    rc = smRegisterServiceCmif(&m_port, smEncodeName(kServiceName), false, 2);
    if (R_FAILED(rc)) {
        eventClose(&m_renderChanged);
        return rc;
    }
    m_startupDeadline = deadlineAfter(2'000'000'000ULL);
    rc = threadCreate(&m_controlThread, controlThreadEntry, this, nullptr,
                      0x6000, 0x2c, -2);
    if (R_FAILED(rc)) {
        smUnregisterServiceCmif(smEncodeName(kServiceName));
        svcCloseHandle(m_port);
        m_port = INVALID_HANDLE;
        eventClose(&m_renderChanged);
        return rc;
    }
    m_threadCreated = true;
    rc = threadStart(&m_controlThread);
    if (R_FAILED(rc)) {
        // The thread never ran, so waiting for its exit would deadlock.
        threadClose(&m_controlThread);
        m_threadCreated = false;
        smUnregisterServiceCmif(smEncodeName(kServiceName));
        svcCloseHandle(m_port);
        m_port = INVALID_HANDLE;
        eventClose(&m_renderChanged);
        return rc;
    }
    return 0;
}

void RendererServer::stop() {
    mutexLock(&m_mutex);
    m_stopControl = true;
    mutexUnlock(&m_mutex);
    eventFire(&m_renderChanged);
    if (m_threadCreated) {
        threadWaitForExit(&m_controlThread);
        threadClose(&m_controlThread);
        m_threadCreated = false;
    }
    eventClose(&m_renderChanged);
}

RenderWork RendererServer::work() {
    mutexLock(&m_mutex);
    const RenderWork result = m_state.work();
    mutexUnlock(&m_mutex);
    return result;
}

Result RendererServer::waitForWork(u64 timeoutNs) {
    return waitSingle(waiterForUEvent(&m_controlChanged), timeoutNs);
}

void RendererServer::signalStateChangedLocked() {
    for (Session &session : m_sessions) {
        if (session.handle != INVALID_HANDLE) eventFire(&session.changed);
    }
}

void RendererServer::publishSuspended(const RenderWork &work, u64 revision,
                                      bool configValid) {
    mutexLock(&m_mutex);
    m_state.publishSuspended(work, revision, configValid);
    if (m_state.ownerReady()) m_orphanDeadline = 0;
    signalStateChangedLocked();
    mutexUnlock(&m_mutex);
    eventFire(&m_renderChanged);
}

bool RendererServer::tryPublishRunning(const RenderWork &work) {
    mutexLock(&m_mutex);
    if (!m_state.canPublishRunning(work)) {
        mutexUnlock(&m_mutex);
        return false;
    }
    m_state.publishRunning(work.lifecycleSequence, work.configRevision);
    signalStateChangedLocked();
    mutexUnlock(&m_mutex);
    eventFire(&m_renderChanged);
    return true;
}

void RendererServer::publishStopped() {
    mutexLock(&m_mutex);
    m_resourcesSafe = true;
    signalStateChangedLocked();
    mutexUnlock(&m_mutex);
    eventFire(&m_renderChanged);
}

void RendererServer::publishFault(Result result) {
    mutexLock(&m_mutex);
    m_state.publishFault(result);
    m_resourcesSafe = true;
    signalStateChangedLocked();
    mutexUnlock(&m_mutex);
    eventFire(&m_renderChanged);
}

void RendererServer::controlThreadEntry(void *argument) {
    static_cast<RendererServer *>(argument)->controlLoop();
}

void RendererServer::disconnectSession(unsigned slot) {
    Handle handle = INVALID_HANDLE;
    Event changed{INVALID_HANDLE, INVALID_HANDLE, false};
    mutexLock(&m_mutex);
    Session &session = m_sessions[slot];
    if (session.handle != INVALID_HANDLE) {
        const SessionIdentity identity{static_cast<u8>(slot),
                                       session.generation};
        handle = session.handle;
        changed = session.changed;
        session.handle = INVALID_HANDLE;
        session.changed = {INVALID_HANDLE, INVALID_HANDLE, false};
        m_state.disconnect(identity);
        if (!m_state.hasOwner())
            m_orphanDeadline = deadlineAfter(2'000'000'000ULL);
        signalStateChangedLocked();
    }
    mutexUnlock(&m_mutex);
    ueventSignal(&m_controlChanged);
    if (eventActive(&changed)) eventClose(&changed);
    if (handle != INVALID_HANDLE) svcCloseHandle(handle);
}

void RendererServer::controlLoop() {
    Handle replyTarget = INVALID_HANDLE;
    while (true) {
        Handle handles[4]{};
        int kinds[4]{};
        int count = 0;
        handles[count] = m_port;
        kinds[count++] = -1;
        handles[count] = m_renderChanged.revent;
        kinds[count++] = -2;
        for (unsigned i = 0; i < 2; ++i) {
            if (m_sessions[i].handle == INVALID_HANDLE) continue;
            handles[count] = m_sessions[i].handle;
            kinds[count++] = static_cast<int>(i);
        }

        mutexLock(&m_mutex);
        const bool stopControl = m_stopControl;
        u64 deadline = minimumDeadline(m_startupDeadline, m_orphanDeadline);
        deadline = minimumDeadline(deadline, m_stopDeadline);
        for (unsigned i = 0; i < 2; ++i) {
            const Session &session = m_sessions[i];
            const SessionIdentity identity{static_cast<u8>(i),
                                           session.generation};
            if (session.handle != INVALID_HANDLE &&
                !m_state.sessionIsOwner(identity))
                deadline = minimumDeadline(
                    deadline,
                    session.acceptedTick + armNsToTicks(2'000'000'000ULL));
        }
        mutexUnlock(&m_mutex);
        if (stopControl) break;

        s32 index = -1;
        const Handle repliedSession = replyTarget;
        const Result waitRc = svcReplyAndReceive(
            &index, handles, count, replyTarget, remainingNs(deadline));
        replyTarget = INVALID_HANDLE;

        if (R_FAILED(waitRc)) {
            int closedSlot = -1;
            if (waitRc == KERNELRESULT(ConnectionClosed) && index >= 0 &&
                index < count && kinds[index] >= 0)
                closedSlot = kinds[index];
            if (waitRc == KERNELRESULT(ConnectionClosed) && closedSlot < 0 &&
                repliedSession != INVALID_HANDLE) {
                for (unsigned i = 0; i < 2; ++i)
                    if (m_sessions[i].handle == repliedSession)
                        closedSlot = static_cast<int>(i);
            }
            if (closedSlot >= 0) {
                disconnectSession(static_cast<unsigned>(closedSlot));
            } else if (waitRc != KERNELRESULT(TimedOut)) {
                svcExitProcess();
            }
        } else if (kinds[index] == -1) {
            for (Session &session : m_sessions) {
                if (session.handle != INVALID_HANDLE) continue;
                Handle accepted = INVALID_HANDLE;
                Event changed{INVALID_HANDLE, INVALID_HANDLE, false};
                if (R_SUCCEEDED(svcAcceptSession(&accepted, m_port)) &&
                    R_SUCCEEDED(eventCreate(&changed, false))) {
                    mutexLock(&m_mutex);
                    session.handle = accepted;
                    session.changed = changed;
                    ++session.generation;
                    if (session.generation == 0) ++session.generation;
                    session.acceptedTick = armGetSystemTick();
                    mutexUnlock(&m_mutex);
                } else {
                    if (eventActive(&changed)) eventClose(&changed);
                    if (accepted != INVALID_HANDLE) svcCloseHandle(accepted);
                }
                break;
            }
        } else if (kinds[index] == -2) {
            eventClear(&m_renderChanged);
        } else {
            const unsigned slot = kinds[index];
            Session &session = m_sessions[slot];
            const SessionIdentity identity{
                static_cast<u8>(slot), session.generation};
            const HipcParsedRequest hipc = hipcParseRequest(armGetTls());
            if (hipc.meta.type == CmifCommandType_Close) {
                disconnectSession(slot);
                continue;
            }

            ParsedRequest request{};
            if (!parseRequest(&request)) {
                makeReply(MAKERESULT(Module_HomebrewAbi, 99), nullptr, 0);
                replyTarget = session.handle;
                continue;
            }

            Result result = 0;
            ProtocolInfo protocol{};
            StatusWire status{};
            Handle copyHandle = INVALID_HANDLE;
            const void *output = nullptr;
            size_t outputSize = 0;
            bool changed = false;
            mutexLock(&m_mutex);
            switch (static_cast<Command>(request.command)) {
                case Command::GetProtocolInfo:
                    output = &protocol;
                    outputSize = sizeof(protocol);
                    break;
                case Command::AcquireStateChangedEvent:
                    copyHandle = session.changed.revent;
                    break;
                case Command::GetStatus:
                    status = m_state.status(identity);
                    output = &status;
                    outputSize = sizeof(status);
                    break;
                case Command::ClaimAndSuspend:
                    result = protocolResult(m_state.claimAndSuspend(
                        identity, *static_cast<const OwnerCommand *>(
                                      request.payload)));
                    if (R_SUCCEEDED(result)) {
                        changed = true;
                        m_startupDeadline = 0;
                    }
                    break;
                case Command::SetConfig:
                    result = protocolResult(m_state.setConfig(
                        identity, *static_cast<const ConfigCommand *>(
                                      request.payload)));
                    changed = R_SUCCEEDED(result);
                    break;
                case Command::Resume:
                    result = protocolResult(m_state.resume(
                        identity, *static_cast<const ResumeCommand *>(
                                      request.payload)));
                    changed = R_SUCCEEDED(result);
                    break;
                case Command::RequestStop:
                    result = protocolResult(m_state.requestStop(
                        identity, *static_cast<const OwnerCommand *>(
                                      request.payload)));
                    if (R_SUCCEEDED(result)) {
                        changed = true;
                        if (m_stopDeadline == 0)
                            m_stopDeadline =
                                deadlineAfter(1'000'000'000ULL);
                    }
                    break;
            }
            if (changed) signalStateChangedLocked();
            mutexUnlock(&m_mutex);
            if (changed) ueventSignal(&m_controlChanged);
            makeReply(result, output, outputSize, copyHandle);
            replyTarget = session.handle;
        }

        const u64 now = armGetSystemTick();
        mutexLock(&m_mutex);
        for (unsigned i = 0; i < 2; ++i) {
            Session &session = m_sessions[i];
            const SessionIdentity identity{static_cast<u8>(i),
                                           session.generation};
            if (session.handle == INVALID_HANDLE ||
                session.handle == replyTarget ||
                m_state.sessionIsOwner(identity) ||
                now < session.acceptedTick + armNsToTicks(2'000'000'000ULL))
                continue;
            eventClose(&session.changed);
            svcCloseHandle(session.handle);
            session.handle = INVALID_HANDLE;
        }
        const bool startupExpired = m_startupDeadline != 0 &&
                                    now >= m_startupDeadline;
        const bool orphanExpired = m_orphanDeadline != 0 &&
                                   now >= m_orphanDeadline;
        const bool stopExpired = m_stopDeadline != 0 && now >= m_stopDeadline;
        const bool cleanStop = m_stopDeadline != 0 && m_resourcesSafe;
        mutexUnlock(&m_mutex);
        if (startupExpired || orphanExpired || stopExpired) svcExitProcess();
        if (cleanStop) break;
    }

    for (Session &session : m_sessions) {
        if (session.handle == INVALID_HANDLE) continue;
        eventClose(&session.changed);
        svcCloseHandle(session.handle);
        session.handle = INVALID_HANDLE;
    }
    if (m_port != INVALID_HANDLE) {
        smUnregisterServiceCmif(smEncodeName(kServiceName));
        svcCloseHandle(m_port);
        m_port = INVALID_HANDLE;
    }
}

} // namespace swots::ipc
