// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <cstdint>

namespace swots::power_policy {

struct State {
    std::uint64_t lastVisualChangeNs = 0;
    std::uint64_t nextPresentNs = 0;
};

struct Decision {
    bool present = true;
    std::uint64_t sleepNs = 0;
    std::uint64_t presentIntervalNs = 0;
};

inline Decision update(State &state, std::uint64_t nowNs,
                       bool visualChanged, bool attentionActive) noexcept {
    constexpr std::uint64_t idleAfterNs = 1'000'000'000ULL;
    constexpr std::uint64_t deepIdleAfterNs = 5'000'000'000ULL;
    constexpr std::uint64_t idleFrameNs = 33'333'333ULL;
    constexpr std::uint64_t deepIdleFrameNs = 100'000'000ULL;
    // Keep lifecycle/button checks responsive even when only presenting at 10 Hz.
    constexpr std::uint64_t maxSleepNs = 50'000'000ULL;

    if (state.lastVisualChangeNs == 0 || visualChanged || attentionActive)
        state.lastVisualChangeNs = nowNs;

    const std::uint64_t quietNs = nowNs - state.lastVisualChangeNs;
    std::uint64_t intervalNs = 0;
    if (quietNs >= deepIdleAfterNs) {
        intervalNs = deepIdleFrameNs;
    } else if (quietNs >= idleAfterNs) {
        intervalNs = idleFrameNs;
    }

    const bool due = state.nextPresentNs == 0 || nowNs >= state.nextPresentNs;
    const bool shouldPresent = visualChanged || attentionActive ||
                               intervalNs == 0 || due;
    if (shouldPresent) state.nextPresentNs = nowNs + intervalNs;

    std::uint64_t sleepNs = 0;
    if (!shouldPresent && state.nextPresentNs > nowNs) {
        sleepNs = std::min(state.nextPresentNs - nowNs, maxSleepNs);
    }
    return {shouldPresent, sleepNs, intervalNs};
}

} // namespace swots::power_policy
