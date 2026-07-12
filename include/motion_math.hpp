// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <cmath>

namespace swots::motion_math {

inline constexpr bool isFreshSamplingNumber(unsigned long long previous,
                                             unsigned long long current) noexcept {
    return current != previous;
}

struct Input {
    float accelX = 0.0f;
    float accelY = 0.0f;
    float gyroX = 0.0f;
    float gyroZ = 0.0f;
    bool valid = false;
};

struct Parameters {
    unsigned sensitivity = 50;
    unsigned smoothing = 50;
};

struct State {
    float velocityX = 0.0f;
    float velocityY = 0.0f;
    float translationX = 0.0f;
    float translationY = 0.0f;
    float turnOffsetX = 0.0f;
    float turnOffsetY = 0.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float roll = 0.0f;
    float gravityX = 0.0f;
    float gravityY = 0.0f;
    float gyroBiasX = 0.0f;
    float gyroBiasZ = 0.0f;
    float filteredAccelX = 0.0f;
    float filteredAccelY = 0.0f;
    float stillSeconds = 0.0f;
    bool gravityReady = false;
};

inline float accelerationResponse(unsigned smoothing, float dt) {
    constexpr float referenceDt = 1.0f / 60.0f;
    const float amount = std::min(smoothing, 100U) / 100.0f;
    const float smoothingExponent = std::exp2((0.5f - amount) * 2.0f);
    return 1.0f - std::pow(0.15f, smoothingExponent * dt / referenceDt);
}

inline float wrap(float value, float extent) {
    if (!std::isfinite(value) || extent <= 0.0f) return 0.0f;
    const float width = extent * 2.0f;
    value = std::fmod(value + extent, width);
    if (value < 0.0f) value += width;
    return value - extent;
}

inline void update(State &state, const Input &input, const Parameters &parameters,
                   float dt) {
    if (!input.valid || !std::isfinite(input.accelX) ||
        !std::isfinite(input.accelY) || !std::isfinite(input.gyroX) ||
        !std::isfinite(input.gyroZ) || !std::isfinite(dt) || dt <= 0.0f) {
        return;
    }

    // A stalled render loop must not integrate a multi-second sensor impulse.
    dt = std::min(dt, 0.1f);
    // 55% maps to roughly 1x, while 100% intentionally has headroom above
    // the reference response for controllers with smaller sensor ranges.
    const float sensitivity =
        0.25f + (std::min(parameters.sensitivity, 100U) / 100.0f) * 1.35f;

    // Clamp corrupt/spiky reports before they can throw the field across the
    // screen. Values remain well above normal Joy-Con operating ranges.
    const float rawAccelX = std::clamp(input.accelX, -8.0f, 8.0f);
    const float rawAccelY = std::clamp(input.accelY, -8.0f, 8.0f);
    const float rawGyroX = std::clamp(input.gyroX, -20.0f, 20.0f);
    const float rawGyroZ = std::clamp(input.gyroZ, -20.0f, 20.0f);

    // Slowly changing acceleration is gravity/controller orientation. Starting
    // from the first report avoids a large impulse on a stationary console.
    if (!state.gravityReady) {
        state.gravityX = rawAccelX;
        state.gravityY = rawAccelY;
        state.gravityReady = true;
    }
    const float gravityResponse = 1.0f - std::exp(-2.5f * dt);
    state.gravityX += (rawAccelX - state.gravityX) * gravityResponse;
    state.gravityY += (rawAccelY - state.gravityY) * gravityResponse;
    const float linearAccelX = rawAccelX - state.gravityX;
    const float linearAccelY = rawAccelY - state.gravityY;
    // The mobile reference gives each new linear-acceleration event weight 0.85. Keep
    // that deliberately fast response near the default, while retaining a
    // useful (but narrow) smoothing control range.
    const float accelResponse = accelerationResponse(parameters.smoothing, dt);
    state.filteredAccelX += (linearAccelX - state.filteredAccelX) * accelResponse;
    state.filteredAccelY += (linearAccelY - state.filteredAccelY) * accelResponse;

    // Learn small gyro offsets only while the acceleration channel is quiet.
    const bool still = std::abs(state.filteredAccelX) < 0.08f &&
                       std::abs(state.filteredAccelY) < 0.08f &&
                       rawGyroX * rawGyroX + rawGyroZ * rawGyroZ < 0.0025f;
    if (still) {
        state.stillSeconds += dt;
        if (state.stillSeconds >= 0.8f) {
            const float biasResponse = 1.0f - std::exp(-dt / 1.5f);
            state.gyroBiasX += (rawGyroX - state.gyroBiasX) * biasResponse;
            state.gyroBiasZ += (rawGyroZ - state.gyroBiasZ) * biasResponse;
        }
    } else {
        state.stillSeconds = 0.0f;
    }
    const float gyroX = rawGyroX - state.gyroBiasX;
    const float gyroZ = rawGyroZ - state.gyroBiasZ;
    // The reference's per-event v-=0.05v and p-=0.1p are approximately 3.08/s and
    // 6.32/s at 60 Hz. This continuous-time form preserves that quick return
    // without making the response depend on renderer/sensor callback rate.
    constexpr float referenceDt = 1.0f / 60.0f;
    const float frameRatio = dt / referenceDt;
    const float velocityRetained = std::pow(0.95f, frameRatio);
    const float velocityInput = referenceDt * (1.0f - velocityRetained) / 0.05f;
    state.velocityX = std::clamp(
        velocityRetained * state.velocityX +
            state.filteredAccelX * sensitivity * velocityInput,
        -8.0f, 8.0f);
    state.velocityY = std::clamp(
        velocityRetained * state.velocityY +
            state.filteredAccelY * sensitivity * velocityInput,
        -8.0f, 8.0f);

    constexpr float translationGain = 20'000.0f;
    const float positionRetained = std::pow(0.90f, frameRatio);
    const float positionInput = translationGain * referenceDt *
                                (1.0f - positionRetained) / 0.10f;
    state.translationX = std::clamp(
        positionRetained * state.translationX + state.velocityX * positionInput,
        -1000.0f, 1000.0f);
    state.translationY = std::clamp(
        positionRetained * state.translationY + state.velocityY * positionInput,
        -1000.0f, 1000.0f);

    // Reference turn-distance gain is 2 * screenWidth pixels/radian. Keep turn phase
    // separate from spring-returning translation so rotation stops immediately
    // with the hand but does not get attenuated by the position return term.
    constexpr float turnGain = 1'280.0f;
    state.turnOffsetX = wrap(
        state.turnOffsetX + gyroZ * turnGain * sensitivity * dt, 104.0f);
    state.turnOffsetY = wrap(
        state.turnOffsetY - gyroX * turnGain * sensitivity * dt, 104.0f);
    state.offsetX = wrap(state.translationX + state.turnOffsetX, 104.0f);
    state.offsetY = wrap(state.translationY + state.turnOffsetY, 104.0f);

    // Until a full 3D gravity projection is calibrated per controller style,
    // keep only a subtle roll cue and avoid integrating large gyro tilt.
    state.roll = std::clamp(state.roll + gyroZ * dt * 0.04f, -0.35f, 0.35f);
}

} // namespace swots::motion_math
