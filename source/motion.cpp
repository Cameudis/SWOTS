// SPDX-License-Identifier: GPL-2.0-or-later

#include "motion.hpp"

#include <algorithm>
#include <cmath>

bool Motion::tryStart(HidNpadIdType id, HidNpadStyleTag style, s32 handleCount) {
    HidSixAxisSensorHandle handles[2]{};
    Result rc = hidGetSixAxisSensorHandles(handles, handleCount, id, style);
    if (R_FAILED(rc)) return false;

    s32 started = 0;
    for (; started < handleCount; ++started) {
        rc = hidStartSixAxisSensor(handles[started]);
        if (R_FAILED(rc)) break;
    }
    if (started != handleCount) {
        while (started > 0) hidStopSixAxisSensor(handles[--started]);
        return false;
    }

    for (s32 index = 0; index < handleCount; ++index) {
        m_handles[index] = handles[index];
        m_samplingNumbers[index] = 0;
    }
    m_handleCount = handleCount;
    m_started = true;
    return true;
}

Motion::Motion() {
    tryStartAny(armGetSystemTick());
}

Motion::~Motion() {
    if (m_started) {
        for (s32 index = 0; index < m_handleCount; ++index)
            hidStopSixAxisSensor(m_handles[index]);
    }
}

MotionSample Motion::sample() {
    MotionSample sample{};
    const u64 now = armGetSystemTick();
    if (!m_started) {
        if (now >= m_nextRetryTick) tryStartAny(now);
        if (!m_started) return sample;
    }

    MotionSample combined{};
    u64 newestSamplingNumber = 0;
    unsigned freshCount = 0;
    for (s32 index = 0; index < m_handleCount; ++index) {
        HidSixAxisSensorState state{};
        if (hidGetSixAxisSensorStates(m_handles[index], &state, 1) == 0)
            continue;
        if (!swots::motion_math::isFreshSamplingNumber(
                m_samplingNumbers[index], state.sampling_number)) continue;

        m_samplingNumbers[index] = state.sampling_number;
        newestSamplingNumber = std::max(newestSamplingNumber, state.sampling_number);
        combined.accelX += state.acceleration.x;
        combined.accelY += state.acceleration.y;
        combined.gyroX += state.angular_velocity.x;
        combined.gyroZ += state.angular_velocity.z;
        ++freshCount;
    }

    if (freshCount == 0) {
        if (armTicksToNs(now - m_lastFreshSampleTick) >= 1'000'000'000ULL) {
            stopAndRetry(now);
        }
        return sample;
    }

    const float divisor = static_cast<float>(freshCount);
    combined.accelX /= divisor;
    combined.accelY /= divisor;
    combined.gyroX /= divisor;
    combined.gyroZ /= divisor;
    combined.valid = true;
    m_lastFreshSampleTick = now;
    m_lastReportedSamplingNumber = newestSamplingNumber;
    m_status = Status::Streaming;
    m_lastSample = combined;
    return combined;
}

void Motion::update(const MotionSample &sample, const Config &config, float dt) {
    swots::motion_math::Input input{sample.accelX, sample.accelY,
                                      sample.gyroX, sample.gyroZ, sample.valid};
    swots::motion_math::Parameters parameters{config.sensitivity,
                                                 config.smoothing};
    swots::motion_math::update(m_state, input, parameters, dt);
}

bool Motion::tryStartAny(u64 now) {
    struct Candidate {
        HidNpadIdType id;
        HidNpadStyleTag style;
        s32 handleCount;
        Source source;
    };
    static constexpr Candidate candidates[] = {
        {HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld, 1, Source::Handheld},
        {HidNpadIdType_No1, HidNpadStyleTag_NpadFullKey, 1, Source::FullKey},
        {HidNpadIdType_No1, HidNpadStyleTag_NpadJoyDual, 2, Source::JoyDual},
    };

    m_source = Source::None;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        const unsigned index = (m_nextSourceCandidate + attempt) % 3;
        const Candidate &candidate = candidates[index];
        if (tryStart(candidate.id, candidate.style, candidate.handleCount)) {
            m_source = candidate.source;
            // If this handle later goes silent, try another controller/style
            // first instead of repeatedly reacquiring the same stale handle.
            m_nextSourceCandidate = (index + 1) % 3;
            break;
        }
    }
    if (!m_started) {
        m_status = Status::WaitingRetry;
        m_nextRetryTick = now + armNsToTicks(1'000'000'000ULL);
        return false;
    }
    m_status = Status::Streaming;
    m_lastFreshSampleTick = now;
    m_lastReportedSamplingNumber = 0;
    m_lastSample = {};
    m_state = {};
    m_nextRetryTick = 0;
    return true;
}

void Motion::stopAndRetry(u64 now) {
    if (m_started) {
        for (s32 index = 0; index < m_handleCount; ++index)
            hidStopSixAxisSensor(m_handles[index]);
    }
    m_started = false;
    m_handleCount = 0;
    m_source = Source::None;
    m_status = Status::WaitingRetry;
    m_lastFreshSampleTick = 0;
    m_nextRetryTick = now + armNsToTicks(1'000'000'000ULL);
}

const char *Motion::sourceName() const {
    switch (m_source) {
        case Source::Handheld: return "Handheld";
        case Source::FullKey: return "FullKey";
        case Source::JoyDual: return "JoyDual";
        default: return "None";
    }
}

const char *Motion::statusName() const {
    switch (m_status) {
        case Status::Streaming: return "Streaming";
        case Status::NoSamples: return "NoSamples";
        default: return "WaitingRetry";
    }
}
