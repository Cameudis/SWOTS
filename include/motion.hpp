// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <switch.h>
#include "config.hpp"
#include "motion_math.hpp"

struct MotionSample {
    float accelX = 0.0f;
    float accelY = 0.0f;
    float gyroX = 0.0f;
    float gyroZ = 0.0f;
    bool valid = false;
};

class Motion {
public:
    enum class Source {
        None,
        Handheld,
        FullKey,
        JoyDual,
    };

    enum class Status {
        WaitingRetry,
        Streaming,
        NoSamples,
    };

    Motion();
    ~Motion();

    MotionSample sample();
    void update(const MotionSample &sample, const Config &config, float dt);

    float offsetX() const { return m_state.offsetX; }
    float offsetY() const { return m_state.offsetY; }
    float roll() const { return m_state.roll; }
    bool available() const { return m_started; }
    Source source() const { return m_source; }
    Status status() const { return m_status; }
    const char *sourceName() const;
    const char *statusName() const;
    u64 samplingNumber() const { return m_lastReportedSamplingNumber; }
    const MotionSample &lastSample() const { return m_lastSample; }

private:
    bool tryStart(HidNpadIdType id, HidNpadStyleTag style, s32 handleCount);
    bool tryStartAny(u64 now);
    void stopAndRetry(u64 now);

    HidSixAxisSensorHandle m_handles[2]{};
    u64 m_samplingNumbers[2]{};
    s32 m_handleCount = 0;
    bool m_started = false;
    Source m_source = Source::None;
    Status m_status = Status::WaitingRetry;
    u64 m_nextRetryTick = 0;
    u64 m_lastFreshSampleTick = 0;
    u64 m_lastReportedSamplingNumber = 0;
    MotionSample m_lastSample{};
    unsigned m_nextSourceCandidate = 0;
    swots::motion_math::State m_state{};
};
