// SPDX-License-Identifier: GPL-2.0-or-later

#include "ipc_client.hpp"

#include "settings_format.hpp"

namespace swots::ipc {
namespace {

u64 deadlineAfter(u64 ns) {
    return armGetSystemTick() + armNsToTicks(ns);
}

u64 remainingNs(u64 deadline) {
    const u64 now = armGetSystemTick();
    return now >= deadline ? 0 : armTicksToNs(deadline - now);
}

bool statusValid(const StatusWire &status) {
    const bool processValid =
        status.processState == ProcessState::Starting ||
        status.processState == ProcessState::Suspended ||
        status.processState == ProcessState::Suspending ||
        status.processState == ProcessState::Running ||
        status.processState == ProcessState::Stopping ||
        status.processState == ProcessState::Fault;
    const auto desiredValid = [](DesiredState state) {
        return state == DesiredState::Suspended ||
               state == DesiredState::Running ||
               state == DesiredState::Stopping;
    };
    return status.magic == kMagic && status.abiMajor == kAbiMajor &&
           status.size == sizeof(StatusWire) && status.reserved[0] == 0 &&
           status.reserved[1] == 0 && status.acceptedConfigValid <= 1 &&
           status.appliedConfigValid <= 1 &&
           status.callerIsOwner <= 1 && processValid &&
           desiredValid(status.desiredState) &&
           desiredValid(status.appliedState);
}

} // namespace

SettingsWire toWire(const settings::Values &values) {
    SettingsWire wire{};
    wire.opacity = values.opacity;
    wire.dotRadius = values.dotRadius;
    wire.sensitivity = values.sensitivity;
    wire.smoothing = values.smoothing;
    return wire;
}

Result ClientWorker::start() {
    mutexInit(&m_mutex);
    mutexInit(&m_submitMutex);
    ueventCreate(&m_requestEvent, false);
    ueventCreate(&m_completionEvent, false);
    Result rc = threadCreate(&m_thread, threadEntry, this, nullptr, 0x5000,
                             0x2c, -2);
    if (R_FAILED(rc)) return rc;
    m_threadCreated = true;
    m_valid = true;
    rc = threadStart(&m_thread);
    if (R_FAILED(rc)) {
        threadClose(&m_thread);
        m_threadCreated = false;
        m_valid = false;
    }
    return rc;
}

bool ClientWorker::shutdown() {
    if (!m_threadCreated) return true;
    const u64 settleDeadline = deadlineAfter(1'000'000'000ULL);
    while (true) {
        mutexLock(&m_mutex);
        const bool pending = m_requestPending;
        mutexUnlock(&m_mutex);
        if (!pending || remainingNs(settleDeadline) == 0) break;
        svcSleepThread(5'000'000ULL);
    }
    mutexLock(&m_mutex);
    const bool canStop = !m_requestPending;
    if (canStop) m_valid = true;
    mutexUnlock(&m_mutex);
    if (!canStop) return false;
    Request request{};
    request.operation = Operation::Shutdown;
    request.deadlineTick = deadlineAfter(1'000'000'000ULL);
    if (R_FAILED(submit(request, 1'000'000'000ULL))) return false;
    threadWaitForExit(&m_thread);
    threadClose(&m_thread);
    m_threadCreated = false;
    m_valid = false;
    return true;
}

Result ClientWorker::connect(u64 timeoutNs) {
    Request request{};
    request.operation = Operation::Connect;
    request.deadlineTick = deadlineAfter(timeoutNs);
    return submit(request, timeoutNs);
}

Result ClientWorker::claimAndSuspend(u64 ownerEpoch, u64 sequence,
                                     u64 timeoutNs) {
    Request request{};
    request.operation = Operation::Claim;
    request.deadlineTick = deadlineAfter(timeoutNs);
    request.owner = {ownerEpoch, sequence};
    return submit(request, timeoutNs);
}

Result ClientWorker::setConfig(u64 ownerEpoch, u64 revision,
                               const SettingsWire &settings, u64 timeoutNs) {
    Request request{};
    request.operation = Operation::SetConfig;
    request.deadlineTick = deadlineAfter(timeoutNs);
    request.config.ownerEpoch = ownerEpoch;
    request.config.configRevision = revision;
    request.config.settings = settings;
    return submit(request, timeoutNs);
}

Result ClientWorker::resume(u64 ownerEpoch, u64 sequence, u64 revision,
                            const SettingsWire &settings, u64 timeoutNs) {
    Request request{};
    request.operation = Operation::Resume;
    request.deadlineTick = deadlineAfter(timeoutNs);
    request.resume.ownerEpoch = ownerEpoch;
    request.resume.lifecycleSequence = sequence;
    request.resume.configRevision = revision;
    request.resume.settings = settings;
    return submit(request, timeoutNs);
}

Result ClientWorker::requestStop(u64 ownerEpoch, u64 sequence,
                                 u64 timeoutNs) {
    Request request{};
    request.operation = Operation::Stop;
    request.deadlineTick = deadlineAfter(timeoutNs);
    request.owner = {ownerEpoch, sequence};
    return submit(request, timeoutNs);
}

void ClientWorker::invalidate() {
    mutexLock(&m_mutex);
    m_valid = false;
    mutexUnlock(&m_mutex);
}

Result ClientWorker::submit(const Request &request, u64 timeoutNs) {
    const u64 submitDeadline = deadlineAfter(timeoutNs);
    while (!mutexTryLock(&m_submitMutex)) {
        if (remainingNs(submitDeadline) == 0) {
            invalidate();
            return KERNELRESULT(TimedOut);
        }
        svcSleepThread(1'000'000ULL);
    }
    mutexLock(&m_mutex);
    if (!m_valid || m_requestPending) {
        mutexUnlock(&m_mutex);
        mutexUnlock(&m_submitMutex);
        return ResultWorkerUnavailable;
    }
    ueventClear(&m_completionEvent);
    m_request = request;
    m_requestPending = true;
    mutexUnlock(&m_mutex);
    ueventSignal(&m_requestEvent);

    const Result waitRc = waitSingle(
        waiterForUEvent(&m_completionEvent), remainingNs(submitDeadline));
    mutexLock(&m_mutex);
    Result result = m_result;
    if (R_FAILED(waitRc)) {
        m_valid = false;
        result = waitRc;
    }
    mutexUnlock(&m_mutex);
    mutexUnlock(&m_submitMutex);
    return result;
}

void ClientWorker::threadEntry(void *argument) {
    static_cast<ClientWorker *>(argument)->threadLoop();
}

void ClientWorker::threadLoop() {
    smInitialize();
    while (true) {
        waitSingle(waiterForUEvent(&m_requestEvent), UINT64_MAX);
        ueventClear(&m_requestEvent);
        mutexLock(&m_mutex);
        if (!m_requestPending) {
            mutexUnlock(&m_mutex);
            continue;
        }
        const Request request = m_request;
        mutexUnlock(&m_mutex);

        const Result result = execute(request);
        const bool shutdown = request.operation == Operation::Shutdown;
        mutexLock(&m_mutex);
        m_result = result;
        m_requestPending = false;
        mutexUnlock(&m_mutex);
        ueventSignal(&m_completionEvent);
        if (shutdown) break;
    }
    closeService();
    smExit();
}

Result ClientWorker::execute(const Request &request) {
    switch (request.operation) {
        case Operation::Connect:
            return connectService(request.deadlineTick);
        case Operation::Claim:
        case Operation::SetConfig:
        case Operation::Resume:
            return waitForApplied(request);
        case Operation::Stop:
            return serviceDispatchIn(
                &m_service, static_cast<u32>(Command::RequestStop),
                request.owner);
        case Operation::Shutdown:
            closeService();
            return 0;
        case Operation::None:
            return ResultWorkerUnavailable;
    }
    return ResultWorkerUnavailable;
}

Result ClientWorker::connectService(u64 deadlineTick) {
    closeService();
    Result rc = ResultWorkerUnavailable;
    do {
        rc = smGetService(&m_service, kServiceName);
        if (R_SUCCEEDED(rc)) break;
        svcSleepThread(20'000'000ULL);
    } while (remainingNs(deadlineTick) != 0);
    if (R_FAILED(rc)) return rc;

    ProtocolInfo info{};
    rc = serviceDispatchOut(
        &m_service, static_cast<u32>(Command::GetProtocolInfo), info);
    if (R_FAILED(rc)) return rc;
    if (!compatible(info)) return ResultVersionMismatch;

    Handle handle = INVALID_HANDLE;
    rc = serviceDispatch(
        &m_service, static_cast<u32>(Command::AcquireStateChangedEvent),
        .out_handle_attrs = {SfOutHandleAttr_HipcCopy},
        .out_handles = &handle);
    if (R_FAILED(rc)) return rc;
    eventLoadRemote(&m_changed, handle, false);
    return 0;
}

Result ClientWorker::getStatus(StatusWire *status) {
    Result rc = serviceDispatchOut(
        &m_service, static_cast<u32>(Command::GetStatus), *status);
    if (R_SUCCEEDED(rc) && !statusValid(*status)) rc = ResultVersionMismatch;
    return rc;
}

Result ClientWorker::waitForApplied(const Request &request) {
    if (!serviceIsActive(&m_service) || !eventActive(&m_changed))
        return ResultWorkerUnavailable;
    eventClear(&m_changed);

    Result rc = 0;
    switch (request.operation) {
        case Operation::Claim:
            rc = serviceDispatchIn(
                &m_service, static_cast<u32>(Command::ClaimAndSuspend),
                request.owner);
            break;
        case Operation::SetConfig:
            rc = serviceDispatchIn(
                &m_service, static_cast<u32>(Command::SetConfig),
                request.config);
            break;
        case Operation::Resume:
            rc = serviceDispatchIn(
                &m_service, static_cast<u32>(Command::Resume), request.resume);
            break;
        default:
            return ResultWorkerUnavailable;
    }
    if (R_FAILED(rc)) return rc;

    while (true) {
        StatusWire status{};
        rc = getStatus(&status);
        if (R_FAILED(rc)) return rc;
        bool complete = false;
        if (request.operation == Operation::Claim) {
            complete = status.callerIsOwner != 0 &&
                       status.ownerEpoch == request.owner.ownerEpoch &&
                       status.appliedState == DesiredState::Suspended &&
                       status.appliedLifecycleSequence >=
                           request.owner.lifecycleSequence;
        } else if (request.operation == Operation::SetConfig) {
            complete = status.callerIsOwner != 0 &&
                       status.appliedConfigValid != 0 &&
                       status.appliedConfigRevision >=
                           request.config.configRevision;
        } else {
            complete = status.callerIsOwner != 0 &&
                       status.appliedState == DesiredState::Running &&
                       status.appliedConfigValid != 0 &&
                       status.appliedLifecycleSequence >=
                           request.resume.lifecycleSequence &&
                       status.appliedConfigRevision >=
                           request.resume.configRevision;
        }
        if (complete) return 0;

        const u64 remaining = remainingNs(request.deadlineTick);
        if (remaining == 0) {
            // One final status read closes the deadline race.
            rc = getStatus(&status);
            if (R_FAILED(rc)) return rc;
            if (request.operation == Operation::Claim &&
                status.callerIsOwner != 0 &&
                status.appliedState == DesiredState::Suspended &&
                status.appliedLifecycleSequence >=
                    request.owner.lifecycleSequence)
                return 0;
            if (request.operation == Operation::SetConfig &&
                status.appliedConfigValid != 0 &&
                status.appliedConfigRevision >= request.config.configRevision)
                return 0;
            if (request.operation == Operation::Resume &&
                status.appliedState == DesiredState::Running &&
                status.appliedConfigValid != 0 &&
                status.appliedLifecycleSequence >=
                    request.resume.lifecycleSequence &&
                status.appliedConfigRevision >=
                    request.resume.configRevision)
                return 0;
            return KERNELRESULT(TimedOut);
        }
        rc = eventWait(&m_changed, remaining);
        if (R_FAILED(rc) && rc != KERNELRESULT(TimedOut)) return rc;
        eventClear(&m_changed);
    }
}

void ClientWorker::closeService() {
    if (eventActive(&m_changed)) eventClose(&m_changed);
    if (serviceIsActive(&m_service)) serviceClose(&m_service);
    m_service = {};
    m_changed = {INVALID_HANDLE, INVALID_HANDLE, false};
}

} // namespace swots::ipc
