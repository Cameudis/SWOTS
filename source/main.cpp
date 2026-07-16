// SPDX-License-Identifier: GPL-2.0-or-later

#include <switch.h>

#include "config.hpp"
#include "control.hpp"
#include "ipc_server.hpp"
#include "motion.hpp"
#include "overlay.hpp"
#include "power_policy.hpp"

extern "C" {
u32 __nx_applet_type = AppletType_None;
u32 __nx_fs_num_sessions = 1;
u32 __nx_fsdev_direntry_cache_size = 1;
bool __nx_fsdev_support_cwd = false;
u32 __nx_nv_transfermem_size = 0x40000;
ViLayerFlags __nx_vi_stray_layer_flags = static_cast<ViLayerFlags>(0);

void __libnx_initheap(void) {
    static char innerHeap[0x600000];
    extern char *fake_heap_start;
    extern char *fake_heap_end;
    fake_heap_start = &innerHeap[0];
    fake_heap_end = &innerHeap[sizeof(innerHeap)];
}

void __wrap_exit(void) {
    svcExitProcess();
    __builtin_unreachable();
}
}

extern "C" void __appInit(void) {
    Result rc = smInitialize();
    if (R_FAILED(rc)) fatalThrow(MAKERESULT(Module_HomebrewLoader, 1));

    rc = setsysInitialize();
    if (R_SUCCEEDED(rc)) {
        SetSysFirmwareVersion firmware{};
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&firmware))) {
            hosversionSet(MAKEHOSVERSION(firmware.major, firmware.minor,
                                         firmware.micro));
        }
        setsysExit();
    }

    rc = fsInitialize();
    if (R_FAILED(rc)) fatalThrow(MAKERESULT(Module_HomebrewLoader, 2));
    rc = hidInitialize();
    if (R_FAILED(rc)) fatalThrow(MAKERESULT(Module_HomebrewLoader, 3));
    padConfigureInput(8, HidNpadStyleSet_NpadStandard |
                             HidNpadStyleTag_NpadSystemExt);
    // SM remains initialized: VI and the private control service both use it.
}

extern "C" void __appExit(void) {
    control::close();
    hidExit();
    fsExit();
    smExit();
}

namespace {

void applySettings(Config *config, const swots::ipc::SettingsWire &settings) {
    config->opacity = settings.opacity;
    config->dotRadius = settings.dotRadius;
    config->sensitivity = settings.sensitivity;
    config->smoothing = settings.smoothing;
}

bool sameWork(const swots::ipc::RenderWork &a,
              const swots::ipc::RenderWork &b) {
    return a.owner == b.owner && a.ownerEpoch == b.ownerEpoch &&
           a.lifecycleSequence == b.lifecycleSequence &&
           a.configRevision == b.configRevision &&
           a.desiredState == b.desiredState;
}

} // namespace

int main(int, char **) {
    // Diagnostics are best effort. Control no longer depends on the SD card.
    control::open();

    swots::ipc::RendererServer server;
    Result rc = server.start();
    if (R_FAILED(rc)) {
        control::writeStatus("ipc-start-failed", rc);
        return static_cast<int>(rc);
    }
    control::writeStatus("ipc-ready", 0);

    Config config{};
    overlay::Context layer{};
    Motion motion;
    swots::power_policy::State powerState{};
    u64 appliedRevision = 0;
    u64 appliedOwnerEpoch = 0;
    bool appliedConfigValid = false;
    u32 viFailureCount = 0;
    Motion::Source lastMotionSource = Motion::Source::None;
    Motion::Status lastMotionStatus = Motion::Status::WaitingRetry;
    bool motionStatusWritten = false;
    u64 nextMotionHeartbeat = 0;
    u64 nextMotionLogAllowed = 0;
    const u64 processStartTick = armGetSystemTick();
    u64 lastTick = processStartTick;

    while (true) {
        swots::ipc::RenderWork work = server.work();
        if (work.ownerEpoch == 0) {
            server.waitForWork(UINT64_MAX);
            continue;
        }

        if (work.desiredState == swots::ipc::DesiredState::Stopping) {
            motion.suspend();
            if (layer.initialized) overlay::fini(&layer);
            server.publishStopped();
            break;
        }

        if (work.configValid &&
            (!appliedConfigValid || work.ownerEpoch != appliedOwnerEpoch ||
             work.configRevision != appliedRevision)) {
            applySettings(&config, work.settings);
            appliedRevision = work.configRevision;
            appliedOwnerEpoch = work.ownerEpoch;
            appliedConfigValid = true;
        }

        if (work.desiredState == swots::ipc::DesiredState::Suspended) {
            motion.suspend();
            if (layer.initialized) overlay::fini(&layer);
            powerState = {};
            server.publishSuspended(work, appliedRevision,
                                    appliedConfigValid);
            server.waitForWork(UINT64_MAX);
            lastTick = armGetSystemTick();
            continue;
        }

        if (!layer.initialized) {
            // Preserve the proven Tesla coexistence cooldown without polling.
            server.waitForWork(500'000'000ULL);
            const swots::ipc::RenderWork afterCooldown = server.work();
            if (!sameWork(work, afterCooldown) ||
                afterCooldown.desiredState !=
                    swots::ipc::DesiredState::Running)
                continue;
            work = afterCooldown;
            applySettings(&config, work.settings);
            appliedRevision = work.configRevision;
            appliedOwnerEpoch = work.ownerEpoch;
            appliedConfigValid = true;

            rc = overlay::init(&layer);
            if (R_FAILED(rc)) {
                motion.suspend();
                if (++viFailureCount >= 3) {
                    control::writeStatus("vi-init-failed", rc);
                    server.publishFault(rc);
                    break;
                }
                server.waitForWork(250'000'000ULL);
                continue;
            }
            viFailureCount = 0;
            powerState = {};
            lastTick = armGetSystemTick();
            if (!server.tryPublishRunning(work)) {
                motion.suspend();
                overlay::fini(&layer);
                continue;
            }
        }

        motion.resume();
        const u64 now = armGetSystemTick();
        float dt = static_cast<float>(armTicksToNs(now - lastTick)) /
                   1'000'000'000.0f;
        lastTick = now;
        if (dt < 0.001f) dt = 0.001f;
        if (dt > 0.100f) dt = 0.100f;

        const overlay::FrameState frame =
            overlay::update(&layer, motion, config, dt);
        const auto power = swots::power_policy::update(
            powerState, armTicksToNs(armGetSystemTick()), frame.visualChanged,
            frame.toastActive);
        if (power.present) overlay::present(&layer, motion, config);

        const bool motionStatusChanged = !motionStatusWritten ||
                                         motion.source() != lastMotionSource ||
                                         motion.status() != lastMotionStatus;
        const bool heartbeatDue = now >= nextMotionHeartbeat;
        if (now >= nextMotionLogAllowed &&
            (motionStatusChanged || heartbeatDue)) {
            const MotionSample &sample = motion.lastSample();
            control::writeSensorStatus(
                motion.sourceName(), motion.statusName(), motion.samplingNumber(),
                static_cast<s32>(sample.accelX * 1000.0f),
                static_cast<s32>(sample.accelY * 1000.0f),
                static_cast<s32>(sample.accelZ * 1000.0f),
                static_cast<s32>(sample.gyroX * 1000.0f),
                static_cast<s32>(sample.gyroY * 1000.0f),
                static_cast<s32>(sample.gyroZ * 1000.0f),
                static_cast<s32>(motion.offsetX() * 100.0f),
                static_cast<s32>(motion.offsetY() * 100.0f),
                armTicksToNs(now - processStartTick) / 1'000'000ULL,
                motion.freshAgeMs(now), motion.signalAgeMs(now),
                motion.retryCount(), motion.startFailureCount(),
                motion.lastAttemptName(), motion.lastStartResult(),
                motion.no1StyleSet(), motion.handheldStyleSet(),
                sample.attributes, motion.consoleInitResult(),
                motion.consoleStartResult());
            lastMotionSource = motion.source();
            lastMotionStatus = motion.status();
            motionStatusWritten = true;
            nextMotionHeartbeat = now + armNsToTicks(30'000'000'000ULL);
            nextMotionLogAllowed = now + armNsToTicks(1'000'000'000ULL);
        }
        if (power.sleepNs != 0) server.waitForWork(power.sleepNs);
    }

    motion.suspend();
    if (layer.initialized) overlay::fini(&layer);
    server.stop();
    return 0;
}
