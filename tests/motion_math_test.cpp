// SPDX-License-Identifier: GPL-2.0-or-later

#include "motion_math.hpp"

#include <cmath>
#include <cstdio>
#include <limits>

using swots::motion_math::Input;
using swots::motion_math::Parameters;
using swots::motion_math::State;

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", \
    __FILE__, __LINE__, #x); ++failures; } } while (false)

void step(State &state, const Input &input, unsigned count,
          Parameters parameters = {}) {
    for (unsigned i = 0; i < count; ++i)
        swots::motion_math::update(state, input, parameters, 1.0f / 60.0f);
}

void validMotionProducesDisplacement() {
    State state;
    step(state, Input{0, 0, -1, 0, 0, 0, true}, 30);
    step(state, Input{1.0f, 0, -1, 0, 0, 0, true}, 15);
    CHECK(std::abs(state.offsetX) > 0.1f);
}

void flatYawRespondsOnFirstMovingFrame() {
    State state;
    step(state, Input{0, 0, -1, 0, 0, 0, true}, 60);
    const float before = state.offsetX;
    swots::motion_math::update(
        state, Input{0, 0, -1, 0, 0, 0.5f, true}, {100, 70},
        1.0f / 60.0f);
    CHECK(std::abs(swots::motion_math::wrap(
              state.offsetX - before, 104.0f)) > 8.0f);
    CHECK(swots::motion_math::wrap(state.offsetX - before, 104.0f) > 0.0f);
}

void uprightYawUsesGyroY() {
    State state;
    step(state, Input{0, -1, 0, 0, 0, 0, true}, 60);
    const float before = state.offsetX;
    swots::motion_math::update(
        state, Input{0, -1, 0, 0, 0.5f, 0, true}, {100, 70},
        1.0f / 60.0f);
    CHECK(std::abs(swots::motion_math::wrap(
              state.offsetX - before, 104.0f)) > 8.0f);
    CHECK(swots::motion_math::wrap(state.offsetX - before, 104.0f) > 0.0f);
}

void sustainedUprightYawKeepsScrolling() {
    State state;
    const Input stationary{0, -1, 0, 0, 0, 0, true};
    const Input turning{0, -1, 0, 0, 0.2f, 0, true};
    step(state, stationary, 60, {50, 50});
    // Continue beyond the 0.8-second stillness/bias-calibration window. A
    // missing gyro-Y stillness check would incorrectly learn this turn away.
    step(state, turning, 120, {50, 50});
    const float before = state.offsetX;
    step(state, turning, 6, {50, 50});
    CHECK(std::abs(swots::motion_math::wrap(
              state.offsetX - before, 104.0f)) > 10.0f);
}

void stationaryGyroResidualDoesNotScroll() {
    State state;
    const Input residual{-0.493f, -0.364f, -0.789f,
                         0.003f, -0.006f, -0.004f, true};
    step(state, residual, 60, {84, 0});
    const float beforeX = state.offsetX;
    const float beforeY = state.offsetY;
    step(state, residual, 300, {84, 0});
    CHECK(std::abs(swots::motion_math::wrap(
              state.offsetX - beforeX, 104.0f)) < 0.01f);
    CHECK(std::abs(swots::motion_math::wrap(
              state.offsetY - beforeY, 104.0f)) < 0.01f);
}

void positiveHorizontalAccelerationMovesPositive() {
    State state;
    step(state, Input{0, 0, -1, 0, 0, 0, true}, 30, {100, 50});
    step(state, Input{1.0f, 0, -1, 0, 0, 0, true}, 3, {100, 50});
    CHECK(state.translationX > 0.0f);
}

void referenceFilterWeightMatchesAtSixtyHz() {
    CHECK(std::abs(swots::motion_math::accelerationResponse(
                       50, 1.0f / 60.0f) - 0.85f) < 0.00001f);
    CHECK(swots::motion_math::accelerationResponse(0, 1.0f / 60.0f) > 0.97f);
    CHECK(swots::motion_math::accelerationResponse(100, 1.0f / 60.0f) > 0.60f);
}

State runGyroAtRate(float dt) {
    State state;
    swots::motion_math::update(
        state, Input{0, 0, -1, 0, 0, 0, true}, {}, dt);
    constexpr float duration = 0.2f; // exactly 6/12/24 frames at 30/60/120 Hz.
    const unsigned frames = static_cast<unsigned>(duration / dt + 0.5f);
    for (unsigned frame = 0; frame < frames; ++frame)
        swots::motion_math::update(
            state, Input{0, 0, -1, 0, 0, 1.0f, true}, {100, 50}, dt);
    return state;
}

void gyroCadenceIsStable() {
    const State at30 = runGyroAtRate(1.0f / 30.0f);
    const State at60 = runGyroAtRate(1.0f / 60.0f);
    const State at120 = runGyroAtRate(1.0f / 120.0f);
    CHECK(std::abs(swots::motion_math::wrap(
              at30.turnOffsetX - at60.turnOffsetX, 104.0f)) < 0.2f);
    CHECK(std::abs(swots::motion_math::wrap(
              at120.turnOffsetX - at60.turnOffsetX, 104.0f)) < 0.2f);
}

State runAccelerationAtRate(float dt) {
    State state;
    const auto run = [&](Input input, float seconds) {
        const unsigned frames = static_cast<unsigned>(seconds / dt + 0.5f);
        for (unsigned frame = 0; frame < frames; ++frame)
            swots::motion_math::update(state, input, {100, 50}, dt);
    };
    run(Input{0, 0, -1, 0, 0, 0, true}, 0.5f);
    run(Input{0.01f, 0, -1, 0, 0, 0, true}, 0.2f);
    run(Input{0, 0, -1, 0, 0, 0, true}, 0.2f);
    return state;
}

void accelerationCadenceIsStable() {
    const State at30 = runAccelerationAtRate(1.0f / 30.0f);
    const State at60 = runAccelerationAtRate(1.0f / 60.0f);
    const State at120 = runAccelerationAtRate(1.0f / 120.0f);
    CHECK(std::abs(swots::motion_math::wrap(
              at30.translationX - at60.translationX, 104.0f)) < 0.2f);
    CHECK(std::abs(swots::motion_math::wrap(
              at120.translationX - at60.translationX, 104.0f)) < 0.2f);
}

void translationStopsWithoutInertia() {
    State state;
    step(state, Input{0, 0, -1, 0, 0, 0, true}, 60, {100, 50});
    step(state, Input{1.0f, 0, -1, 0, 0, 0, true}, 6, {100, 50});
    step(state, Input{0, 0, -1, 0, 0, 0, true}, 12, {100, 50});
    const float stopped = state.translationX;
    step(state, Input{0, 0, -1, 0, 0, 0, true}, 60, {100, 50});
    CHECK(std::abs(swots::motion_math::wrap(
              state.translationX - stopped, 104.0f)) < 0.5f);
}

void translationDoesNotSpringBack() {
    State state;
    const Input stationary{0, 0, -1, 0, 0, 0, true};
    state.translationX = 80.0f;
    step(state, stationary, 120, {100, 50});
    CHECK(std::abs(state.translationX - 80.0f) < 0.00001f);
}

void invalidReportsDoNotJump() {
    State state;
    step(state, Input{0, 0, -1, 0, 0, 0, true}, 5);
    const State before = state;
    step(state, Input{8, 8, 8, 8, 8, 8, false}, 30);
    swots::motion_math::update(
        state, Input{std::numeric_limits<float>::quiet_NaN(), 0, -1,
                     0, 0, 0, true}, {}, 1.0f / 60.0f);
    CHECK(state.offsetX == before.offsetX);
    CHECK(state.offsetY == before.offsetY);
    CHECK(state.roll == before.roll);
}

void sensitivityChangesResponse() {
    State low, high;
    step(low, Input{0, 0, -1, 0, 0, 0, true}, 10, {0, 50});
    step(high, Input{0, 0, -1, 0, 0, 0, true}, 10, {100, 50});
    step(low, Input{1, 0, -1, 0, 0, 0, true}, 15, {0, 50});
    step(high, Input{1, 0, -1, 0, 0, 0, true}, 15, {100, 50});
    CHECK(std::abs(high.translationX) > std::abs(low.translationX) * 3.0f);
}

void clampsAndWraps() {
    State state;
    step(state, Input{0, 0, -1, 0, 0, 0, true}, 1);
    for (unsigned i = 0; i < 10000; ++i)
        swots::motion_math::update(
            state, Input{1e6f, -1e6f, 1e6f, 1e6f, -1e6f, 1e6f, true},
            {100, 0}, 5.0f);
    CHECK(state.offsetX >= -104.0f && state.offsetX < 104.0f);
    CHECK(state.offsetY >= -104.0f && state.offsetY < 104.0f);
    CHECK(state.roll >= -0.35f && state.roll <= 0.35f);
    CHECK(swots::motion_math::wrap(100000.0f, 104.0f) >= -104.0f);
    CHECK(swots::motion_math::wrap(100000.0f, 104.0f) < 104.0f);
}

void stationaryGravityConvergesWithoutContinuedDrive() {
    State state;
    const Input stationary{0.35f, -0.72f, -0.60f, 0, 0, 0, true};
    step(state, stationary, 600);
    const float before = state.offsetX;
    step(state, stationary, 300);
    CHECK(std::abs(swots::motion_math::wrap(state.offsetX - before, 104.0f)) < 0.05f);
}

void changedTiltConvergesWithoutContinuedDrive() {
    State state;
    const Input flat{0, 0, -1, 0, 0, 0, true};
    constexpr float diagonal = 0.70710678f;
    const Input tilted{0, -diagonal, -diagonal, 0, 0, 0, true};
    step(state, flat, 60);
    step(state, tilted, 300);
    const float before = state.offsetY;
    step(state, tilted, 300);
    CHECK(std::abs(swots::motion_math::wrap(
              state.offsetY - before, 104.0f)) < 0.05f);
}

void shortAccelerationStillMovesField() {
    State state;
    step(state, Input{0.2f, -0.8f, -0.56f, 0, 0, 0, true}, 180);
    const float before = state.offsetY;
    step(state, Input{0.2f, 0.2f, -0.56f, 0, 0, 0, true}, 12);
    CHECK(std::abs(swots::motion_math::wrap(state.offsetY - before, 104.0f)) > 0.1f);
}
} // namespace

int main() {
    CHECK(!swots::motion_math::isFreshSamplingNumber(42, 42));
    CHECK(swots::motion_math::isFreshSamplingNumber(42, 43));
    CHECK(swots::motion_math::isFreshSamplingNumber(
        std::numeric_limits<unsigned long long>::max(), 0));
    CHECK(!swots::motion_math::hasUsableGravity(0.0f, 0.0f, 0.0f));
    CHECK(!swots::motion_math::hasUsableGravity(0.01f, -0.01f, 0.01f));
    CHECK(swots::motion_math::hasUsableGravity(0.0f, 0.0f, -1.0f));
    CHECK(!swots::motion_math::hasUsableGravity(
        std::numeric_limits<float>::quiet_NaN(), 0.0f, -1.0f));
    CHECK(swots::motion_math::shouldHoldLastSample(0));
    CHECK(swots::motion_math::shouldHoldLastSample(149'999'999ULL));
    CHECK(!swots::motion_math::shouldHoldLastSample(150'000'000ULL));
    CHECK(swots::motion_math::isInactiveControllerPlaceholder(
        0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f));
    CHECK(!swots::motion_math::isInactiveControllerPlaceholder(
        0.01f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f));
    CHECK(!swots::motion_math::isInactiveControllerPlaceholder(
        0.0f, 0.0f, -1.0f, 0.0f, 0.001f, 0.0f));
    validMotionProducesDisplacement();
    flatYawRespondsOnFirstMovingFrame();
    uprightYawUsesGyroY();
    sustainedUprightYawKeepsScrolling();
    stationaryGyroResidualDoesNotScroll();
    positiveHorizontalAccelerationMovesPositive();
    referenceFilterWeightMatchesAtSixtyHz();
    gyroCadenceIsStable();
    accelerationCadenceIsStable();
    translationStopsWithoutInertia();
    translationDoesNotSpringBack();
    invalidReportsDoNotJump();
    sensitivityChangesResponse();
    clampsAndWraps();
    stationaryGravityConvergesWithoutContinuedDrive();
    changedTiltConvergesWithoutContinuedDrive();
    shortAccelerationStillMovesField();
    if (failures) return 1;
    std::puts("motion math tests passed");
    return 0;
}
