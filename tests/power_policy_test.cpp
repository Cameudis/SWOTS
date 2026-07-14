// SPDX-License-Identifier: GPL-2.0-or-later

#include "power_policy.hpp"

#include <cstdio>

using swots::power_policy::Decision;
using swots::power_policy::State;

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", \
    __FILE__, __LINE__, #x); ++failures; } } while (false)

void motionKeepsFullRate() {
    State state;
    CHECK(swots::power_policy::update(state, 1, true, false).present);
    const Decision moving =
        swots::power_policy::update(state, 2'000'000'000ULL, true, false);
    CHECK(moving.present);
    CHECK(moving.presentIntervalNs == 0);
}

void quietFrameRateStepsDown() {
    State state;
    swots::power_policy::update(state, 1, true, false);
    const Decision idle =
        swots::power_policy::update(state, 1'000'000'001ULL, false, false);
    CHECK(idle.presentIntervalNs == 33'333'333ULL);
    const Decision deep =
        swots::power_policy::update(state, 5'000'000'001ULL, false, false);
    CHECK(deep.presentIntervalNs == 100'000'000ULL);
}

void skippedFramesHaveBoundedSleep() {
    State state;
    swots::power_policy::update(state, 1, true, false);
    const Decision firstIdle =
        swots::power_policy::update(state, 5'000'000'001ULL, false, false);
    CHECK(firstIdle.present);
    const Decision skipped =
        swots::power_policy::update(state, 5'010'000'001ULL, false, false);
    CHECK(!skipped.present);
    CHECK(skipped.sleepNs == 50'000'000ULL);
}

void toastForcesImmediateFrames() {
    State state;
    swots::power_policy::update(state, 1, true, false);
    const Decision toast =
        swots::power_policy::update(state, 8'000'000'000ULL, false, true);
    CHECK(toast.present);
    CHECK(toast.presentIntervalNs == 0);
}
} // namespace

int main() {
    motionKeepsFullRate();
    quietFrameRateStepsDown();
    skippedFramesHaveBoundedSleep();
    toastForcesImmediateFrames();
    if (failures) return 1;
    std::puts("power policy tests passed");
    return 0;
}
