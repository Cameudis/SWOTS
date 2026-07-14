// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <switch.h>
#include "config.hpp"
#include "motion_math.hpp"
#include "source_selection.hpp"

struct MotionSample {
    float accelX = 0.0f;
    float accelY = 0.0f;
    float accelZ = 0.0f;
    float gyroX = 0.0f;
    float gyroY = 0.0f;
    float gyroZ = 0.0f;
    u32 attributes = 0;
    bool valid = false;
};

class Motion {
public:
    enum class Source {
        None,
        Console,
        Handheld,
        FullKey,
        JoyDual,
    };

    enum class Status {
        WaitingRetry,
        Streaming,
        NoSamples,
        ZeroData,
    };

    Motion();
    ~Motion();

    void resume();
    void suspend();
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
    u64 freshAgeMs(u64 now) const;
    u64 signalAgeMs(u64 now) const;
    u32 retryCount() const { return m_retryCount; }
    u32 startFailureCount() const { return m_startFailureCount; }
    const char *lastAttemptName() const;
    Result lastStartResult() const { return m_lastStartResult; }
    u32 no1StyleSet() const { return m_no1StyleSet; }
    u32 handheldStyleSet() const { return m_handheldStyleSet; }
    Result consoleInitResult() const { return m_consoleInitResult; }
    Result consoleStartResult() const { return m_consoleStartResult; }

private:
    bool tryStartConsole(u64 now);
    Result tryStartController(HidNpadIdType id, HidNpadStyleTag style,
                              s32 handleCount);
    bool tryStartAny(u64 now);
    bool switchToController(u64 now);
    bool switchToConsole(u64 now);
    void prepareStreaming(u64 now, bool resetCalibration);
    void stopAndRetry(u64 now, u64 delayNs = 1'000'000'000ULL);
    void releaseCurrent();
    void refreshStyleSets();
    void refreshProbeHandles(u64 now);
    unsigned readControllerStates(const HidSixAxisSensorHandle *handles,
                                  u64 *samplingNumbers, s32 handleCount,
                                  MotionSample *sample,
                                  u64 *newestSamplingNumber);
    bool probeHighQualityController(u64 now, MotionSample *sample);
    void resetInputCalibration();
    bool activeStyleAvailable() const;

    HidSixAxisSensorHandle m_handles[2]{};
    u64 m_samplingNumbers[2]{};
    HidSixAxisSensorHandle m_probeHandles[2]{};
    u64 m_probeSamplingNumbers[2]{};
    s32 m_probeHandleCount = 0;
    Source m_probeSource = Source::None;
    HidNpadIdType m_probeId = HidNpadIdType_Other;
    HidNpadStyleTag m_probeStyle = static_cast<HidNpadStyleTag>(0);
    u64 m_nextProbeRefreshTick = 0;
    u64 m_nextProbeSampleTick = 0;
    u64 m_nextActiveStyleCheckTick = 0;
    u64 m_probeLastSamplingNumber = 0;
    u64 m_consoleSamplingNumber = 0;
    s32 m_handleCount = 0;
    bool m_started = false;
    bool m_running = false;
    bool m_consoleInitialized = false;
    bool m_consoleStarted = false;
    Source m_source = Source::None;
    Status m_status = Status::WaitingRetry;
    u64 m_nextRetryTick = 0;
    u64 m_lastFreshSampleTick = 0;
    u64 m_lastPhysicalSignalTick = 0;
    u64 m_lastReportedSamplingNumber = 0;
    MotionSample m_lastSample{};
    Source m_lastAttemptSource = Source::None;
    Result m_lastStartResult = 0;
    u32 m_retryCount = 0;
    u32 m_startFailureCount = 0;
    HidNpadIdType m_activeId = HidNpadIdType_Other;
    HidNpadStyleTag m_activeStyle = static_cast<HidNpadStyleTag>(0);
    u32 m_no1StyleSet = 0;
    u32 m_handheldStyleSet = 0;
    u64 m_consoleRetryTick = 0;
    Result m_consoleInitResult = 0;
    Result m_consoleStartResult = 0;
    swots::source_selection::State m_sourceSelection{};
    swots::motion_math::State m_state{};
};
