// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <switch.h>

#include "settings_format.hpp"

struct Color {
    u8 r;
    u8 g;
    u8 b;
    u8 a;
};

struct Config : swots::settings::Values {
    bool enabled = false;
    u8 dotSpacing = 52;
    Color dotColor = {0xF4, 0xF7, 0xFF, 0xFF};
};

// A 640x360 framebuffer is scaled by VI to the full 1920x1080 logical display.
// This keeps the demo below 2 MiB of framebuffer + shadow-buffer memory.
inline constexpr u32 FRAMEBUFFER_WIDTH = 640;
inline constexpr u32 FRAMEBUFFER_HEIGHT = 360;
inline constexpr s32 DISPLAY_WIDTH = 1920;
inline constexpr s32 DISPLAY_HEIGHT = 1080;

// Keep the tested program ID stable so existing Atmosphere installs upgrade in
// place. The public project name changed before its first open-source release.
inline constexpr u64 SWOTS_TITLE_ID = 0x4200000000007E09ULL;
inline constexpr const char *SWOTS_CONFIG_DIR = "/config/swots";
inline constexpr const char *SWOTS_ENABLED_FILE = "/config/swots/enabled.flag";
inline constexpr const char *SWOTS_TESLA_LIFECYCLE_FILE =
    "/config/swots/tesla_lifecycle.bin";
inline constexpr const char *SWOTS_TESLA_LIFECYCLE_TEMP_FILE =
    "/config/swots/tesla_lifecycle.tmp";
inline constexpr const char *SWOTS_TESLA_LIFECYCLE_ACK_FILE =
    "/config/swots/tesla_lifecycle.ack";
inline constexpr const char *SWOTS_TESLA_LIFECYCLE_ACK_TEMP_FILE =
    "/config/swots/tesla_lifecycle.ack.tmp";
inline constexpr const char *SWOTS_LOG_FILE = "/config/swots/renderer.log";
inline constexpr const char *SWOTS_SENSOR_LOG_FILE = "/config/swots/sensor.log";
inline constexpr const char *SWOTS_SETTINGS_FILE = "/config/swots/settings.cfg";
inline constexpr const char *SWOTS_SETTINGS_TEMP_FILE = "/config/swots/settings.tmp";
inline constexpr const char *SWOTS_SETTINGS_BACKUP_FILE = "/config/swots/settings.bak";
