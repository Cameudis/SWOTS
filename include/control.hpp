// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <switch.h>

namespace control {

Result open();
void close();
void writeStatus(const char *stage, Result result);
void writeSensorStatus(const char *source, const char *status,
                       u64 samplingNumber, s32 accelXMilli, s32 accelYMilli,
                       s32 accelZMilli, s32 gyroXMilli, s32 gyroYMilli,
                       s32 gyroZMilli, s32 offsetXCenti, s32 offsetYCenti,
                       u64 uptimeMs, u64 freshAgeMs, u64 signalAgeMs,
                       u32 retryCount, u32 startFailureCount,
                       const char *lastAttempt, Result lastStartResult,
                       u32 no1StyleSet, u32 handheldStyleSet,
                       u32 sampleAttributes, Result consoleInitResult,
                       Result consoleStartResult);

} // namespace control
