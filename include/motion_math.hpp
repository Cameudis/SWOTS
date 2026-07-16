// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <cmath>

namespace swots::motion_math {

inline constexpr bool isFreshSamplingNumber(unsigned long long previous,
                                             unsigned long long current) noexcept {
    return current != previous;
}

inline bool hasUsableGravity(float accelX, float accelY, float accelZ) noexcept {
    if (!std::isfinite(accelX) || !std::isfinite(accelY) ||
        !std::isfinite(accelZ)) return false;
    // A live six-axis report includes gravity even while the controller is at
    // rest. A tiny norm is the zero-filled stream returned by some inactive
    // controller/style combinations, not a stationary physical sensor.
    constexpr float minimumGravity = 0.10f;
    return accelX * accelX + accelY * accelY + accelZ * accelZ >=
           minimumGravity * minimumGravity;
}

inline constexpr bool shouldHoldLastSample(
    unsigned long long freshAgeNs) noexcept {
    // ConsoleSevenSixAxisSensor reports at roughly 10 Hz. Treat its most
    // recent acceleration/angular velocity as a zero-order-held signal so the
    // 60 Hz motion model keeps integrating and damping between reports. Stop
    // after 150 ms so a stalled stream cannot keep driving motion indefinitely.
    return freshAgeNs < 150'000'000ULL;
}

inline bool isInactiveControllerPlaceholder(float accelX, float accelY,
                                             float accelZ, float gyroX,
                                             float gyroY,
                                             float gyroZ) noexcept {
    if (!std::isfinite(accelX) || !std::isfinite(accelY) ||
        !std::isfinite(accelZ) || !std::isfinite(gyroX) ||
        !std::isfinite(gyroY) || !std::isfinite(gyroZ)) return false;
    constexpr float epsilon = 0.00001f;
    return std::abs(accelX) <= epsilon && std::abs(accelY) <= epsilon &&
           std::abs(accelZ + 1.0f) <= epsilon &&
           std::abs(gyroX) <= epsilon && std::abs(gyroY) <= epsilon &&
           std::abs(gyroZ) <= epsilon;
}

struct Input {
    float accelX = 0.0f;
    float accelY = 0.0f;
    float accelZ = 0.0f;
    float gyroX = 0.0f;
    float gyroY = 0.0f;
    float gyroZ = 0.0f;
    bool valid = false;
};

struct Parameters {
    unsigned sensitivity = 50;
    unsigned smoothing = 50;
};

struct State {
    float translationX = 0.0f;
    float translationY = 0.0f;
    float turnOffsetX = 0.0f;
    float turnOffsetY = 0.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float roll = 0.0f;
    float gravityX = 0.0f;
    float gravityY = 0.0f;
    float gravityZ = 0.0f;
    float lastAccelX = 0.0f;
    float lastAccelY = 0.0f;
    float lastAccelZ = 0.0f;
    float gyroBiasX = 0.0f;
    float gyroBiasY = 0.0f;
    float gyroBiasZ = 0.0f;
    float filteredAccelX = 0.0f;
    float filteredAccelY = 0.0f;
    float sensorStableSeconds = 0.0f;
    float stillSeconds = 0.0f;
    bool accelHistoryReady = false;
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

inline float deadband(float value, float threshold) {
    if (value > threshold) return value - threshold;
    if (value < -threshold) return value + threshold;
    return 0.0f;
}

inline void update(State &state, const Input &input, const Parameters &parameters,
                   float dt) {
    if (!input.valid || !std::isfinite(input.accelX) ||
        !std::isfinite(input.accelY) || !std::isfinite(input.accelZ) ||
        !std::isfinite(input.gyroX) || !std::isfinite(input.gyroY) ||
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
    const float rawAccelZ = std::clamp(input.accelZ, -8.0f, 8.0f);
    const float rawGyroX = std::clamp(input.gyroX, -20.0f, 20.0f);
    const float rawGyroY = std::clamp(input.gyroY, -20.0f, 20.0f);
    const float rawGyroZ = std::clamp(input.gyroZ, -20.0f, 20.0f);

    // Slowly changing acceleration is gravity/controller orientation. Starting
    // from the first report avoids a large impulse on a stationary console.
    if (!state.gravityReady) {
        state.gravityX = rawAccelX;
        state.gravityY = rawAccelY;
        state.gravityZ = rawAccelZ;
        state.gravityReady = true;
    }
    const float rawGyroMagnitudeSq = rawGyroX * rawGyroX +
                                     rawGyroY * rawGyroY +
                                     rawGyroZ * rawGyroZ;
    if (state.accelHistoryReady) {
        const float deltaX = rawAccelX - state.lastAccelX;
        const float deltaY = rawAccelY - state.lastAccelY;
        const float deltaZ = rawAccelZ - state.lastAccelZ;
        constexpr float stableAccelDeltaSq = 0.01f * 0.01f;
        constexpr float stableGyroMagnitudeSq = 0.01f * 0.01f;
        if (deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ <=
                stableAccelDeltaSq &&
            rawGyroMagnitudeSq <= stableGyroMagnitudeSq) {
            state.sensorStableSeconds += dt;
        } else {
            state.sensorStableSeconds = 0.0f;
        }
    } else {
        state.accelHistoryReady = true;
    }
    state.lastAccelX = rawAccelX;
    state.lastAccelY = rawAccelY;
    state.lastAccelZ = rawAccelZ;

    const float gravityResponse = 1.0f - std::exp(-2.5f * dt);
    state.gravityX += (rawAccelX - state.gravityX) * gravityResponse;
    state.gravityY += (rawAccelY - state.gravityY) * gravityResponse;
    state.gravityZ += (rawAccelZ - state.gravityZ) * gravityResponse;
    const float linearAccelX = rawAccelX - state.gravityX;
    const float linearAccelY = rawAccelY - state.gravityY;
    // The mobile reference gives each new linear-acceleration event weight 0.85. Keep
    // that deliberately fast response near the default, while retaining a
    // useful (but narrow) smoothing control range.
    const float accelResponse = accelerationResponse(parameters.smoothing, dt);
    state.filteredAccelX += (linearAccelX - state.filteredAccelX) * accelResponse;
    state.filteredAccelY += (linearAccelY - state.filteredAccelY) * accelResponse;

    // Once both channels are stable, the current acceleration vector is the
    // stationary gravity pose for cue purposes. Snap to it and clear filter
    // residue so holding any tilt cannot produce continuing translation.
    constexpr float stationarySettleSeconds = 0.12f;
    if (state.sensorStableSeconds >= stationarySettleSeconds) {
        state.gravityX = rawAccelX;
        state.gravityY = rawAccelY;
        state.gravityZ = rawAccelZ;
        state.filteredAccelX = 0.0f;
        state.filteredAccelY = 0.0f;
    }

    // Learn small gyro offsets only while the acceleration channel is quiet.
    const bool still = std::abs(state.filteredAccelX) < 0.08f &&
                       std::abs(state.filteredAccelY) < 0.08f &&
                       rawGyroX * rawGyroX + rawGyroY * rawGyroY +
                               rawGyroZ * rawGyroZ <
                           0.0025f;
    if (still) {
        state.stillSeconds += dt;
        if (state.stillSeconds >= 0.8f) {
            const float biasResponse = 1.0f - std::exp(-dt / 1.5f);
            state.gyroBiasX += (rawGyroX - state.gyroBiasX) * biasResponse;
            state.gyroBiasY += (rawGyroY - state.gyroBiasY) * biasResponse;
            state.gyroBiasZ += (rawGyroZ - state.gyroBiasZ) * biasResponse;
        }
    } else {
        state.stillSeconds = 0.0f;
    }
    // Retail logs show stationary residuals around 0.003-0.006 rad/s. Match
    // the reference's 0.01 rad/s deadband so those offsets cannot become a
    // constant-speed scroll while preserving deliberate rotation.
    constexpr float gyroDeadband = 0.01f;
    const float gyroX = deadband(rawGyroX - state.gyroBiasX, gyroDeadband);
    const float gyroY = deadband(rawGyroY - state.gyroBiasY, gyroDeadband);
    const float gyroZ = deadband(rawGyroZ - state.gyroBiasZ, gyroDeadband);
    // Acceleration directly controls field scroll rate. There is deliberately
    // no accumulated velocity: once the filtered sensor signal reaches the
    // deadband, the dots stop at their current phase instead of coasting or
    // springing back.
    constexpr float accelerationDeadband = 0.025f;
    constexpr float translationRateGain = 180.0f;
    const float translationRateX =
        deadband(state.filteredAccelX, accelerationDeadband) *
        translationRateGain * sensitivity;
    const float translationRateY =
        deadband(state.filteredAccelY, accelerationDeadband) *
        translationRateGain * sensitivity;
    state.translationX = wrap(
        state.translationX + translationRateX * dt, 104.0f);
    state.translationY = wrap(
        state.translationY + translationRateY * dt, 104.0f);

    // Project device-frame angular velocity onto world-up, estimated from the
    // low-pass gravity vector. This keeps yaw on gyro Z while the console is
    // flat and moves it to gyro Y when the screen is held upright. The minus
    // sign preserves the existing flat-console direction because a stationary
    // sensor reports gravity near (0, 0, -1).
    float yawRate = gyroZ;
    const float gravityNormSq = state.gravityX * state.gravityX +
                                state.gravityY * state.gravityY +
                                state.gravityZ * state.gravityZ;
    if (gravityNormSq >= 0.01f) {
        yawRate = -(gyroX * state.gravityX + gyroY * state.gravityY +
                    gyroZ * state.gravityZ) /
                  std::sqrt(gravityNormSq);
    }

    // Reference turn-distance gain is 2 * screenWidth pixels/radian. Keep turn
    // phase separate from acceleration-driven translation so rotation stops
    // immediately with the hand.
    constexpr float turnGain = 1'280.0f;
    state.turnOffsetX = wrap(
        state.turnOffsetX + yawRate * turnGain * sensitivity * dt, 104.0f);
    state.turnOffsetY = wrap(
        state.turnOffsetY - gyroX * turnGain * sensitivity * dt, 104.0f);
    state.offsetX = wrap(state.translationX + state.turnOffsetX, 104.0f);
    state.offsetY = wrap(state.translationY + state.turnOffsetY, 104.0f);

    // Until a full 3D gravity projection is calibrated per controller style,
    // keep only a subtle roll cue and avoid integrating large gyro tilt.
    state.roll = std::clamp(state.roll + gyroZ * dt * 0.04f, -0.35f, 0.35f);
}

} // namespace swots::motion_math
