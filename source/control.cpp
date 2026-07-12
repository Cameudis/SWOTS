// SPDX-License-Identifier: GPL-2.0-or-later

#include "control.hpp"
#include "config.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace control {

static FsFileSystem g_sd{};
static bool g_open = false;

Result open() {
    if (g_open) return 0;
    Result rc = fsOpenSdCardFileSystem(&g_sd);
    if (R_SUCCEEDED(rc)) g_open = true;
    return rc;
}

void close() {
    if (!g_open) return;
    fsFsClose(&g_sd);
    g_open = false;
}

bool isEnabled() {
    if (!g_open) return false;
    FsFile file{};
    Result rc = fsFsOpenFile(&g_sd, SWOTS_ENABLED_FILE, FsOpenMode_Read, &file);
    if (R_FAILED(rc)) return false;
    fsFileClose(&file);
    return true;
}

bool readTeslaLifecycle(tesla_coexistence::LifecycleSignal *signal) {
    if (!g_open || signal == nullptr) return false;
    *signal = {};

    FsFile file{};
    if (R_FAILED(fsFsOpenFile(&g_sd, SWOTS_TESLA_LIFECYCLE_FILE,
                              FsOpenMode_Read, &file))) {
        return false;
    }

    tesla_lifecycle::Record record{};
    u64 bytesRead = 0;
    const Result rc = fsFileRead(&file, 0, &record, sizeof(record),
                                 FsReadOption_None, &bytesRead);
    fsFileClose(&file);
    if (R_FAILED(rc) || bytesRead != sizeof(record) ||
        !tesla_lifecycle::valid(record)) {
        return false;
    }

    signal->present = true;
    signal->session = record.session;
    signal->generation = record.generation;
    signal->state = record.state;
    return true;
}

Result writeTeslaLifecycleAck(u64 session, u64 generation) {
    if (!g_open || session == 0 || generation == 0) {
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }

    tesla_lifecycle::Ack ack{};
    ack.session = session;
    ack.generation = generation;

    fsFsDeleteFile(&g_sd, SWOTS_TESLA_LIFECYCLE_ACK_TEMP_FILE);
    Result rc = fsFsCreateFile(&g_sd, SWOTS_TESLA_LIFECYCLE_ACK_TEMP_FILE,
                               sizeof(ack), 0);
    if (R_FAILED(rc)) return rc;

    FsFile file{};
    rc = fsFsOpenFile(&g_sd, SWOTS_TESLA_LIFECYCLE_ACK_TEMP_FILE,
                      FsOpenMode_Write, &file);
    if (R_SUCCEEDED(rc)) {
        rc = fsFileWrite(&file, 0, &ack, sizeof(ack), FsWriteOption_Flush);
        fsFileClose(&file);
    }
    if (R_FAILED(rc)) return rc;
    rc = fsFsCommit(&g_sd);
    if (R_FAILED(rc)) return rc;

    fsFsDeleteFile(&g_sd, SWOTS_TESLA_LIFECYCLE_ACK_FILE);
    rc = fsFsRenameFile(&g_sd, SWOTS_TESLA_LIFECYCLE_ACK_TEMP_FILE,
                        SWOTS_TESLA_LIFECYCLE_ACK_FILE);
    if (R_FAILED(rc)) return rc;
    return fsFsCommit(&g_sd);
}

namespace {

bool loadSettingsFile(const char *path, const swots::settings::Values &base,
                      swots::settings::Values *values) {
    FsFile file{};
    if (R_FAILED(fsFsOpenFile(&g_sd, path, FsOpenMode_Read, &file))) return false;

    s64 fileSize = 0;
    Result rc = fsFileGetSize(&file, &fileSize);
    if (R_FAILED(rc) || fileSize <= 0 ||
        fileSize > static_cast<s64>(swots::settings::kMaxTextSize)) {
        fsFileClose(&file);
        return false;
    }

    char text[swots::settings::kMaxTextSize]{};
    u64 bytesRead = 0;
    rc = fsFileRead(&file, 0, text, static_cast<u64>(fileSize),
                    FsReadOption_None, &bytesRead);
    fsFileClose(&file);
    if (R_FAILED(rc) || bytesRead != static_cast<u64>(fileSize)) return false;

    return swots::settings::parse(text, static_cast<std::size_t>(fileSize),
                                     base, values);
}

} // namespace

bool loadSettings(Config *config) {
    if (!g_open || config == nullptr) return false;

    const swots::settings::Values base = *config;
    swots::settings::Values loaded{};
    constexpr const char *candidates[] = {
        SWOTS_SETTINGS_FILE,
        SWOTS_SETTINGS_TEMP_FILE,
        SWOTS_SETTINGS_BACKUP_FILE,
    };
    for (const char *path : candidates) {
        if (!loadSettingsFile(path, base, &loaded)) continue;
        config->opacity = loaded.opacity;
        config->dotRadius = loaded.dotRadius;
        config->sensitivity = loaded.sensitivity;
        config->smoothing = loaded.smoothing;
        return true;
    }
    return false;
}

Result setEnabled(bool enabled) {
    if (!g_open) {
        Result rc = open();
        if (R_FAILED(rc)) return rc;
    }

    // Already-exists / not-found are harmless for an idempotent toggle.
    fsFsCreateDirectory(&g_sd, "/config");
    fsFsCreateDirectory(&g_sd, SWOTS_CONFIG_DIR);

    if (enabled) {
        FsFile file{};
        Result rc = fsFsOpenFile(&g_sd, SWOTS_ENABLED_FILE, FsOpenMode_Read, &file);
        if (R_SUCCEEDED(rc)) {
            fsFileClose(&file);
            return 0;
        }
        rc = fsFsCreateFile(&g_sd, SWOTS_ENABLED_FILE, 0, 0);
        if (R_FAILED(rc)) return rc;
    } else {
        FsFile file{};
        Result rc = fsFsOpenFile(&g_sd, SWOTS_ENABLED_FILE, FsOpenMode_Read, &file);
        if (R_FAILED(rc)) return 0;
        fsFileClose(&file);
        rc = fsFsDeleteFile(&g_sd, SWOTS_ENABLED_FILE);
        if (R_FAILED(rc)) return rc;
    }

    return fsFsCommit(&g_sd);
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
                                     "stage=%s\nresult=0x%08X\n",
                                     stage, static_cast<unsigned>(result));
    if (length <= 0) return;
    writeSmallFile(SWOTS_LOG_FILE, text,
                   std::min<int>(length, sizeof(text) - 1));
}

void writeSensorStatus(const char *source, const char *status, u64 samplingNumber,
                       s32 accelXMilli, s32 accelYMilli,
                       s32 gyroXMilli, s32 gyroZMilli,
                       s32 offsetXCenti, s32 offsetYCenti) {
    if (!g_open || !source || !status) return;
    char text[192]{};
    const int length = std::snprintf(text, sizeof(text),
                                     "source=%s\nstatus=%s\nsampling=%llu\n"
                                     "accel_milli=%d,%d\ngyro_milli=%d,%d\n"
                                     "offset_centi=%d,%d\n",
                                     source, status,
                                     static_cast<unsigned long long>(samplingNumber),
                                     accelXMilli, accelYMilli,
                                     gyroXMilli, gyroZMilli,
                                     offsetXCenti, offsetYCenti);
    if (length <= 0) return;
    writeSmallFile(SWOTS_SENSOR_LOG_FILE, text,
                   std::min<int>(length, sizeof(text) - 1));
}

u64 readTeslaCombo() {
    constexpr const char *path = "/config/tesla/config.ini";
    constexpr u64 fallback = HidNpadButton_L | HidNpadButton_Down |
                             HidNpadButton_StickR;
    if (!g_open) return fallback;

    FsFile file{};
    if (R_FAILED(fsFsOpenFile(&g_sd, path, FsOpenMode_Read, &file))) return fallback;

    char data[256]{};
    u64 bytesRead = 0;
    fsFileRead(&file, 0, data, sizeof(data) - 1, FsReadOption_None, &bytesRead);
    fsFileClose(&file);
    data[std::min<u64>(bytesRead, sizeof(data) - 1)] = '\0';

    const char *value = std::strstr(data, "key_combo=");
    if (!value) return fallback;
    value += std::strlen("key_combo=");

    u64 combo = 0;
    char token[16]{};
    u32 tokenLength = 0;
    auto addToken = [&]() {
        token[tokenLength] = '\0';
        struct Mapping { const char *name; u64 button; };
        static constexpr Mapping mappings[] = {
            {"A", HidNpadButton_A}, {"B", HidNpadButton_B},
            {"X", HidNpadButton_X}, {"Y", HidNpadButton_Y},
            {"L", HidNpadButton_L}, {"R", HidNpadButton_R},
            {"ZL", HidNpadButton_ZL}, {"ZR", HidNpadButton_ZR},
            {"PLUS", HidNpadButton_Plus}, {"MINUS", HidNpadButton_Minus},
            {"DUP", HidNpadButton_Up}, {"DDOWN", HidNpadButton_Down},
            {"DLEFT", HidNpadButton_Left}, {"DRIGHT", HidNpadButton_Right},
            {"LSTICK", HidNpadButton_StickL}, {"RSTICK", HidNpadButton_StickR},
        };
        for (const auto &mapping : mappings) {
            if (std::strcmp(token, mapping.name) == 0) {
                combo |= mapping.button;
                break;
            }
        }
        tokenLength = 0;
    };

    while (*value && *value != '\r' && *value != '\n') {
        if (*value == '+') {
            addToken();
        } else if (tokenLength + 1 < sizeof(token)) {
            token[tokenLength++] = *value;
        }
        ++value;
    }
    addToken();
    return combo ? combo : fallback;
}

} // namespace control
