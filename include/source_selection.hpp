// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>

namespace swots::source_selection {

enum class Event {
    None,
    PreferController,
    PreferConsole,
};

struct State {
    std::uint64_t qualitySinceNs = 0;
    std::uint64_t qualityLastSeenNs = 0;
    std::uint64_t lossSinceNs = 0;
    std::uint64_t cooldownUntilNs = 0;
};

inline Event update(State &state, std::uint64_t nowNs,
                    bool controllerSelected,
                    bool highQualityControllerSample) noexcept {
    constexpr std::uint64_t upgradeConfirmNs = 600'000'000ULL;
    constexpr std::uint64_t fallbackConfirmNs = 800'000'000ULL;
    constexpr std::uint64_t switchCooldownNs = 3'000'000'000ULL;
    constexpr std::uint64_t probeGapToleranceNs = 120'000'000ULL;

    if (controllerSelected) {
        state.qualitySinceNs = 0;
        state.qualityLastSeenNs = 0;
        if (highQualityControllerSample) {
            state.lossSinceNs = 0;
            return Event::None;
        }
        if (state.lossSinceNs == 0) state.lossSinceNs = nowNs;
        if (nowNs >= state.cooldownUntilNs &&
            nowNs - state.lossSinceNs >= fallbackConfirmNs) {
            state.lossSinceNs = 0;
            state.cooldownUntilNs = nowNs + switchCooldownNs;
            return Event::PreferConsole;
        }
        return Event::None;
    }

    state.lossSinceNs = 0;
    if (!highQualityControllerSample) {
        if (state.qualityLastSeenNs != 0 &&
            nowNs - state.qualityLastSeenNs <= probeGapToleranceNs) {
            return Event::None;
        }
        state.qualitySinceNs = 0;
        state.qualityLastSeenNs = 0;
        return Event::None;
    }
    if (state.qualitySinceNs == 0 || state.qualityLastSeenNs == 0 ||
        nowNs - state.qualityLastSeenNs > probeGapToleranceNs) {
        state.qualitySinceNs = nowNs;
    }
    state.qualityLastSeenNs = nowNs;
    if (nowNs >= state.cooldownUntilNs &&
        nowNs - state.qualitySinceNs >= upgradeConfirmNs) {
        state.qualitySinceNs = 0;
        state.qualityLastSeenNs = 0;
        state.cooldownUntilNs = nowNs + switchCooldownNs;
        return Event::PreferController;
    }
    return Event::None;
}

} // namespace swots::source_selection
