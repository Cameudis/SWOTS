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
    step(state, Input{0, 0, 0, 0, true}, 30);
    step(state, Input{1.0f, 0, 0, 0, true}, 15);
    CHECK(std::abs(state.offsetX) > 0.1f);
}

void gyroRespondsOnFirstMovingFrame() {
    State state;
    step(state, Input{0, 0, 0, 0, true}, 60);
    const float before = state.offsetX;
    swots::motion_math::update(
        state, Input{0, 0, 0, 0.5f, true}, {100, 70}, 1.0f / 60.0f);
    CHECK(std::abs(swots::motion_math::wrap(
              state.offsetX - before, 104.0f)) > 8.0f);
    CHECK(swots::motion_math::wrap(state.offsetX - before, 104.0f) > 0.0f);
}

void positiveHorizontalAccelerationMovesPositive() {
    State state;
    step(state, Input{0, 0, 0, 0, true}, 30, {100, 50});
    step(state, Input{1.0f, 0, 0, 0, true}, 3, {100, 50});
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
    swots::motion_math::update(state, Input{0, 0, 0, 0, true}, {}, dt);
    constexpr float duration = 0.2f; // exactly 6/12/24 frames at 30/60/120 Hz.
    const unsigned frames = static_cast<unsigned>(duration / dt + 0.5f);
    for (unsigned frame = 0; frame < frames; ++frame)
        swots::motion_math::update(
            state, Input{0, 0, 0, 1.0f, true}, {100, 50}, dt);
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
    run(Input{0, 0, 0, 0, true}, 0.5f);
    run(Input{1.0f, 0, 0, 0, true}, 0.2f);
    run(Input{0, 0, 0, 0, true}, 0.2f);
    return state;
}

void accelerationCadenceIsStable() {
    const State at30 = runAccelerationAtRate(1.0f / 30.0f);
    const State at60 = runAccelerationAtRate(1.0f / 60.0f);
    const State at120 = runAccelerationAtRate(1.0f / 120.0f);
    const float reference = std::max(1.0f, std::abs(at60.translationX));
    CHECK(std::abs(at30.translationX - at60.translationX) / reference < 0.08f);
    CHECK(std::abs(at120.translationX - at60.translationX) / reference < 0.08f);
}

void translationReturnsQuicklyAfterImpulse() {
    State state;
    step(state, Input{0, 0, 0, 0, true}, 60, {100, 50});
    step(state, Input{1.0f, 0, 0, 0, true}, 6, {100, 50});
    const float peak = std::abs(state.translationX);
    CHECK(peak > 5.0f);
    step(state, Input{0, 0, 0, 0, true}, 120, {100, 50});
    CHECK(std::abs(state.translationX) < peak * 0.25f);
}

void invalidReportsDoNotJump() {
    State state;
    step(state, Input{0, 0, 0, 0, true}, 5);
    const State before = state;
    step(state, Input{8, 8, 8, 8, false}, 30);
    swots::motion_math::update(
        state, Input{std::numeric_limits<float>::quiet_NaN(), 0, 0, 0, true},
        {}, 1.0f / 60.0f);
    CHECK(state.offsetX == before.offsetX);
    CHECK(state.offsetY == before.offsetY);
    CHECK(state.roll == before.roll);
}

void sensitivityChangesResponse() {
    State low, high;
    step(low, Input{0, 0, 0, 0, true}, 10, {0, 50});
    step(high, Input{0, 0, 0, 0, true}, 10, {100, 50});
    step(low, Input{1, 0, 0, 0, true}, 15, {0, 50});
    step(high, Input{1, 0, 0, 0, true}, 15, {100, 50});
    CHECK(std::abs(high.translationX) > std::abs(low.translationX) * 3.0f);
}

void clampsAndWraps() {
    State state;
    step(state, Input{0, 0, 0, 0, true}, 1);
    for (unsigned i = 0; i < 10000; ++i)
        swots::motion_math::update(state, Input{1e6f, -1e6f, 1e6f, -1e6f, true},
                                      {100, 0}, 5.0f);
    CHECK(state.offsetX >= -104.0f && state.offsetX < 104.0f);
    CHECK(state.offsetY >= -104.0f && state.offsetY < 104.0f);
    CHECK(state.roll >= -0.35f && state.roll <= 0.35f);
    CHECK(std::abs(state.velocityX) <= 250.0f);
    CHECK(std::abs(state.velocityY) <= 250.0f);
    CHECK(swots::motion_math::wrap(100000.0f, 104.0f) >= -104.0f);
    CHECK(swots::motion_math::wrap(100000.0f, 104.0f) < 104.0f);
}

void stationaryGravityConvergesWithoutContinuedDrive() {
    State state;
    const Input stationary{0.35f, -0.72f, 0, 0, true};
    step(state, stationary, 600);
    const float before = state.offsetX;
    const float velocityBefore = std::abs(state.velocityX);
    step(state, stationary, 300);
    CHECK(std::abs(state.velocityX) < 0.01f);
    CHECK(std::abs(state.velocityX) <= velocityBefore + 0.001f);
    CHECK(std::abs(swots::motion_math::wrap(state.offsetX - before, 104.0f)) < 0.05f);
}

void shortAccelerationStillMovesField() {
    State state;
    step(state, Input{0.2f, -0.8f, 0, 0, true}, 180);
    const float before = state.offsetY;
    step(state, Input{0.2f, 0.2f, 0, 0, true}, 12);
    CHECK(std::abs(swots::motion_math::wrap(state.offsetY - before, 104.0f)) > 0.1f);
}
} // namespace

int main() {
    CHECK(!swots::motion_math::isFreshSamplingNumber(42, 42));
    CHECK(swots::motion_math::isFreshSamplingNumber(42, 43));
    CHECK(swots::motion_math::isFreshSamplingNumber(
        std::numeric_limits<unsigned long long>::max(), 0));
    validMotionProducesDisplacement();
    gyroRespondsOnFirstMovingFrame();
    positiveHorizontalAccelerationMovesPositive();
    referenceFilterWeightMatchesAtSixtyHz();
    gyroCadenceIsStable();
    accelerationCadenceIsStable();
    translationReturnsQuicklyAfterImpulse();
    invalidReportsDoNotJump();
    sensitivityChangesResponse();
    clampsAndWraps();
    stationaryGravityConvergesWithoutContinuedDrive();
    shortAccelerationStillMovesField();
    if (failures) return 1;
    std::puts("motion math tests passed");
    return 0;
}
