// SPDX-License-Identifier: GPL-2.0-or-later

#include "control.hpp"
#include "config.hpp"

#include <algorithm>
#include <cstdio>

namespace control {

static FsFileSystem g_sd{};
static bool g_open = false;

Result open() {
    if (g_open) return 0;
    const Result rc = fsOpenSdCardFileSystem(&g_sd);
    if (R_SUCCEEDED(rc)) g_open = true;
    return rc;
}

void close() {
    if (!g_open) return;
    fsFsClose(&g_sd);
    g_open = false;
}

namespace {

void writeSmallFile(const char *path, const char *text, int length) {
    if (!g_open || !path || !text || length <= 0) return;
    fsFsCreateDirectory(&g_sd, "/config");
    fsFsCreateDirectory(&g_sd, SWOTS_CONFIG_DIR);
    fsFsDeleteFile(&g_sd, path);
    const u64 size = static_cast<u64>(length);
    if (R_FAILED(fsFsCreateFile(&g_sd, path, size, 0))) return;
    FsFile file{};
    if (R_FAILED(fsFsOpenFile(&g_sd, path, FsOpenMode_Write, &file))) return;
    fsFileWrite(&file, 0, text, size, FsWriteOption_Flush);
    fsFileClose(&file);
    fsFsCommit(&g_sd);
}

} // namespace

void writeStatus(const char *stage, Result result) {
    if (!g_open || !stage) return;
    char text[160]{};
    const int length = std::snprintf(text, sizeof(text),
                                     "stage=%s\nresult=0x%08X\n", stage,
                                     static_cast<unsigned>(result));
    if (length > 0)
        writeSmallFile(SWOTS_LOG_FILE, text,
                       std::min<int>(length, sizeof(text) - 1));
}

void writeSensorStatus(const char *source, const char *status,
                       u64 samplingNumber, s32 accelXMilli, s32 accelYMilli,
                       s32 accelZMilli, s32 gyroXMilli, s32 gyroYMilli,
                       s32 gyroZMilli, s32 offsetXCenti, s32 offsetYCenti,
                       u64 uptimeMs, u64 freshAgeMs, u64 signalAgeMs,
                       u32 retryCount, u32 startFailureCount,
                       const char *lastAttempt, Result lastStartResult,
                       u32 no1StyleSet, u32 handheldStyleSet,
                       u32 sampleAttributes, Result consoleInitResult,
                       Result consoleStartResult) {
    if (!g_open || !source || !status || !lastAttempt) return;
    char text[768]{};
    const int length = std::snprintf(
        text, sizeof(text),
        "---\nuptime_ms=%llu\nsource=%s\nstatus=%s\nsampling=%llu\n"
        "fresh_age_ms=%llu\nsignal_age_ms=%llu\n"
        "accel_milli=%d,%d,%d\ngyro_milli=%d,%d,%d\n"
        "offset_centi=%d,%d\nretry_count=%u\nstart_failures=%u\n"
        "last_attempt=%s\nlast_start_result=0x%08X\n"
        "styles_no1=0x%08X\nstyles_handheld=0x%08X\n"
        "sample_attributes=0x%08X\nconsole_init_result=0x%08X\n"
        "console_start_result=0x%08X\n",
        static_cast<unsigned long long>(uptimeMs), source, status,
        static_cast<unsigned long long>(samplingNumber),
        static_cast<unsigned long long>(freshAgeMs),
        static_cast<unsigned long long>(signalAgeMs), accelXMilli,
        accelYMilli, accelZMilli, gyroXMilli, gyroYMilli, gyroZMilli,
        offsetXCenti, offsetYCenti, static_cast<unsigned>(retryCount),
        static_cast<unsigned>(startFailureCount), lastAttempt,
        static_cast<unsigned>(lastStartResult),
        static_cast<unsigned>(no1StyleSet),
        static_cast<unsigned>(handheldStyleSet),
        static_cast<unsigned>(sampleAttributes),
        static_cast<unsigned>(consoleInitResult),
        static_cast<unsigned>(consoleStartResult));
    if (length <= 0) return;

    fsFsCreateDirectory(&g_sd, "/config");
    fsFsCreateDirectory(&g_sd, SWOTS_CONFIG_DIR);
    FsFile file{};
    Result rc = fsFsOpenFile(&g_sd, SWOTS_SENSOR_LOG_FILE,
                             FsOpenMode_Write, &file);
    if (R_FAILED(rc)) {
        rc = fsFsCreateFile(&g_sd, SWOTS_SENSOR_LOG_FILE, 0, 0);
        if (R_FAILED(rc) ||
            R_FAILED(fsFsOpenFile(&g_sd, SWOTS_SENSOR_LOG_FILE,
                                  FsOpenMode_Write, &file)))
            return;
    }
    s64 offset = 0;
    if (R_FAILED(fsFileGetSize(&file, &offset)) || offset < 0) {
        fsFileClose(&file);
        return;
    }
    constexpr s64 maxLogSize = 32 * 1024;
    const s64 recordSize = std::min<int>(length, sizeof(text) - 1);
    if (offset + recordSize > maxLogSize) {
        if (R_FAILED(fsFileSetSize(&file, 0))) {
            fsFileClose(&file);
            return;
        }
        offset = 0;
    }
    rc = fsFileSetSize(&file, offset + recordSize);
    if (R_SUCCEEDED(rc))
        rc = fsFileWrite(&file, offset, text, recordSize,
                         FsWriteOption_Flush);
    fsFileClose(&file);
    if (R_SUCCEEDED(rc)) fsFsCommit(&g_sd);
}

} // namespace control
