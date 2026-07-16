// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>

namespace swots::ipc {

inline constexpr char kServiceName[] = "swots:u";
inline constexpr std::uint32_t kMagic = 0x50495753U; // "SWIP"
inline constexpr std::uint16_t kAbiMajor = 1;
inline constexpr std::uint16_t kAbiMinor = 0;
inline constexpr std::uint32_t kBuildVersion = 1;

inline constexpr std::uint64_t kCapabilityStateEvent = 1ULL << 0;
inline constexpr std::uint64_t kCapabilityRuntimeConfig = 1ULL << 1;
inline constexpr std::uint64_t kCapabilitySessionOwnership = 1ULL << 2;
inline constexpr std::uint64_t kRequiredCapabilities =
    kCapabilityStateEvent | kCapabilityRuntimeConfig |
    kCapabilitySessionOwnership;

enum class Command : std::uint32_t {
    GetProtocolInfo = 0,
    AcquireStateChangedEvent = 1,
    GetStatus = 2,
    ClaimAndSuspend = 3,
    SetConfig = 4,
    Resume = 5,
    RequestStop = 6,
};

enum class ProcessState : std::uint8_t {
    Starting = 1,
    Suspended = 2,
    Suspending = 3,
    Running = 4,
    Stopping = 5,
    Fault = 6,
};

enum class DesiredState : std::uint8_t {
    Suspended = 1,
    Running = 2,
    Stopping = 3,
};

struct ProtocolInfo {
    std::uint32_t magic = kMagic;
    std::uint16_t abiMajor = kAbiMajor;
    std::uint16_t abiMinor = kAbiMinor;
    std::uint32_t size = sizeof(ProtocolInfo);
    std::uint32_t reserved0 = 0;
    std::uint64_t capabilities = kRequiredCapabilities;
    std::uint32_t rendererVersion = kBuildVersion;
    std::uint32_t reserved1 = 0;
};
static_assert(sizeof(ProtocolInfo) == 32);

struct SettingsWire {
    std::uint32_t size = sizeof(SettingsWire);
    std::uint16_t version = 1;
    std::uint8_t opacity = 210;
    std::uint8_t dotRadius = 5;
    std::uint8_t sensitivity = 55;
    std::uint8_t smoothing = 70;
    std::uint8_t reserved[6]{};
};
static_assert(sizeof(SettingsWire) == 16);

struct OwnerCommand {
    std::uint64_t ownerEpoch = 0;
    std::uint64_t lifecycleSequence = 0;
};
static_assert(sizeof(OwnerCommand) == 16);

struct ConfigCommand {
    std::uint64_t ownerEpoch = 0;
    std::uint64_t configRevision = 0;
    SettingsWire settings{};
};
static_assert(sizeof(ConfigCommand) == 32);

struct ResumeCommand {
    std::uint64_t ownerEpoch = 0;
    std::uint64_t lifecycleSequence = 0;
    std::uint64_t configRevision = 0;
    SettingsWire settings{};
};
static_assert(sizeof(ResumeCommand) == 40);

struct StatusWire {
    std::uint32_t magic = kMagic;
    std::uint16_t abiMajor = kAbiMajor;
    std::uint16_t abiMinor = kAbiMinor;
    std::uint32_t size = sizeof(StatusWire);
    ProcessState processState = ProcessState::Starting;
    DesiredState desiredState = DesiredState::Suspended;
    DesiredState appliedState = DesiredState::Suspended;
    std::uint8_t callerIsOwner = 0;
    std::uint64_t ownerEpoch = 0;
    std::uint64_t appliedLifecycleSequence = 0;
    std::uint64_t acceptedConfigRevision = 0;
    std::uint64_t appliedConfigRevision = 0;
    std::uint32_t lastResult = 0;
    std::uint8_t acceptedConfigValid = 0;
    std::uint8_t appliedConfigValid = 0;
    std::uint8_t reserved[2]{};
};
static_assert(sizeof(StatusWire) == 56);

inline constexpr bool validSettings(const SettingsWire &settings) noexcept {
    if (settings.size != sizeof(SettingsWire) || settings.version != 1 ||
        settings.dotRadius < 2 || settings.dotRadius > 12 ||
        settings.sensitivity > 100 || settings.smoothing > 100)
        return false;
    for (std::uint8_t value : settings.reserved)
        if (value != 0) return false;
    return true;
}

inline constexpr bool compatible(const ProtocolInfo &info) noexcept {
    return info.magic == kMagic && info.size == sizeof(ProtocolInfo) &&
           info.abiMajor == kAbiMajor && info.reserved0 == 0 &&
           info.reserved1 == 0 &&
           info.rendererVersion == kBuildVersion &&
           (info.capabilities & kRequiredCapabilities) ==
               kRequiredCapabilities;
}

} // namespace swots::ipc
