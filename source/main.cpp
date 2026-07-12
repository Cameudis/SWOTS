// SPDX-License-Identifier: GPL-2.0-or-later

// SWOTS sysmodule demo.
//
// The process is intentionally launchable through pmshell (the same path used
// by ovl-sysmodules). It stays alive while disabled and creates the VI layer
// only while the Tesla-side enabled.flag exists.

#include <switch.h>

#include <cmath>

#include "config.hpp"
#include "control.hpp"
#include "motion.hpp"
#include "overlay.hpp"
#include "tesla_coexistence.hpp"

extern "C" {
u32 __nx_applet_type = AppletType_None;
u32 __nx_fs_num_sessions = 1;
u32 __nx_fsdev_direntry_cache_size = 1;
bool __nx_fsdev_support_cwd = false;
u32 __nx_nv_transfermem_size = 0x40000;
ViLayerFlags __nx_vi_stray_layer_flags = static_cast<ViLayerFlags>(0);

// Match nx-ovlloader: sysmodules need a usable heap before main is entered.
// framebufferCreate plus the linear shadow buffer use this heap. Match
// nx-ovlloader's proven 6 MiB allocation to leave room for NV metadata.
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

    // nx-ovlloader reads the firmware version before initializing services
    // which dispatch different IPC commands by HOS version.
    rc = setsysInitialize();
    if (R_SUCCEEDED(rc)) {
        SetSysFirmwareVersion firmware{};
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&firmware))) {
            hosversionSet(MAKEHOSVERSION(firmware.major, firmware.minor, firmware.micro));
        }
        setsysExit();
    }

    rc = fsInitialize();
    if (R_FAILED(rc)) fatalThrow(MAKERESULT(Module_HomebrewLoader, 2));

    rc = hidInitialize();
    if (R_FAILED(rc)) fatalThrow(MAKERESULT(Module_HomebrewLoader, 3));

    padConfigureInput(8, HidNpadStyleSet_NpadStandard | HidNpadStyleTag_NpadSystemExt);
    // Keep SM alive: viInitialize() runs later when cues are enabled and must
    // obtain vi:m through this session. Closing SM here yields 0xE401
    // (KernelError_InvalidHandle).
}

extern "C" void __appExit(void) {
    hidExit();
    fsExit();
    smExit();
}

int main(int, char **) {
    // Remain alive and retry after an SD-card transient instead of turning a
    // one-off mount failure into a process that can never be enabled.
    while (R_FAILED(control::open())) svcSleepThread(1'000'000'000ULL);

    Config config{};
    Motion motion;
    overlay::Context layer{};
    PadState pad{};
    padInitializeAny(&pad);
    const u64 teslaCombo = control::readTeslaCombo();
    tesla_coexistence::StateMachine teslaState;
    u64 nextSettingsReload = 0;
    Motion::Source lastMotionSource = Motion::Source::None;
    Motion::Status lastMotionStatus = Motion::Status::WaitingRetry;
    bool motionStatusWritten = false;
    bool motionObserved = false;
    u32 motionStatusWrites = 0;
    u64 lastTick = armGetSystemTick();

    while (true) {
        padUpdate(&pad);
        const u64 buttons = padGetButtons(&pad);
        const bool comboHeld = (buttons & teslaCombo) == teslaCombo;
        const u64 loopTick = armGetSystemTick();
        if (loopTick >= nextSettingsReload) {
            control::loadSettings(&config);
            nextSettingsReload = loopTick + armNsToTicks(500'000'000ULL);
        }
        config.enabled = control::isEnabled();
        const u64 nowNs = armTicksToNs(loopTick);
        tesla_coexistence::LifecycleSignal lifecycle{};
        control::readTeslaLifecycle(&lifecycle);
        const auto coexistence =
            teslaState.update(config.enabled, comboHeld, lifecycle, nowNs);

        if (coexistence.destroyLayer && layer.initialized) overlay::fini(&layer);
        if (coexistence.acknowledgeLifecycle) {
            control::writeTeslaLifecycleAck(coexistence.lifecycleSession,
                                            coexistence.lifecycleGeneration);
        }
        if (coexistence.event == tesla_coexistence::Event::Suspended) {
            control::writeStatus("suspended-for-tesla", 0);
        } else if (coexistence.event == tesla_coexistence::Event::Closing) {
            control::writeStatus("tesla-closing", 0);
        }

        if (!config.enabled) {
            svcSleepThread(100'000'000ULL);
            lastTick = armGetSystemTick();
            continue;
        }

        if (!coexistence.allowLayer) {
            svcSleepThread(20'000'000ULL);
            lastTick = armGetSystemTick();
            continue;
        }

        if (!layer.initialized) {
            Result rc = overlay::init(&layer);
            if (R_FAILED(rc)) {
                // Avoid a crash loop when a firmware rejects VI manager access.
                svcSleepThread(1'000'000'000ULL);
                continue;
            }
            lastTick = armGetSystemTick();
        }

        const u64 now = armGetSystemTick();
        float dt = static_cast<float>(armTicksToNs(now - lastTick)) / 1'000'000'000.0f;
        lastTick = now;
        if (dt < 0.001f) dt = 0.001f;
        if (dt > 0.100f) dt = 0.100f;

        overlay::render(&layer, motion, config, dt);
        const bool motionStatusChanged = !motionStatusWritten ||
                                         motion.source() != lastMotionSource ||
                                         motion.status() != lastMotionStatus;
        const bool movedNow = std::abs(motion.offsetX()) >= 1.0f ||
                              std::abs(motion.offsetY()) >= 1.0f;
        if (motion.source() != lastMotionSource) motionObserved = false;
        const bool shouldWriteMotion = (!motionObserved && movedNow) ||
                                       (motionStatusChanged && motionStatusWrites < 12);
        if (shouldWriteMotion) {
            const MotionSample &sample = motion.lastSample();
            control::writeSensorStatus(
                motion.sourceName(), motion.statusName(), motion.samplingNumber(),
                static_cast<s32>(sample.accelX * 1000.0f),
                static_cast<s32>(sample.accelY * 1000.0f),
                static_cast<s32>(sample.gyroX * 1000.0f),
                static_cast<s32>(sample.gyroZ * 1000.0f),
                static_cast<s32>(motion.offsetX() * 100.0f),
                static_cast<s32>(motion.offsetY() * 100.0f));
            lastMotionSource = motion.source();
            lastMotionStatus = motion.status();
            motionStatusWritten = true;
            motionObserved = motionObserved || movedNow;
            ++motionStatusWrites;
        }
    }
}
