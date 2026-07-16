// SPDX-License-Identifier: GPL-2.0-or-later

#include "motion.hpp"

#include <algorithm>

namespace {

constexpr u64 kRetryDelayNs = 1'000'000'000ULL;
constexpr u64 kConsoleRetryDelayNs = 30'000'000'000ULL;
constexpr u64 kPassiveProbeIntervalNs = 50'000'000ULL;

struct ControllerCandidate {
    HidNpadIdType id;
    HidNpadStyleTag style;
    s32 handleCount;
    Motion::Source source;
};

constexpr ControllerCandidate kControllerCandidates[] = {
    {HidNpadIdType_No1, HidNpadStyleTag_NpadFullKey, 1,
     Motion::Source::FullKey},
    {HidNpadIdType_No1, HidNpadStyleTag_NpadJoyDual, 2,
     Motion::Source::JoyDual},
    {HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld, 1,
     Motion::Source::Handheld},
};

} // namespace

bool Motion::tryStartConsole(u64 now) {
    if (now < m_consoleRetryTick) return false;

    m_lastAttemptSource = Source::Console;
    m_consoleInitResult = hidInitializeSevenSixAxisSensor();
    m_lastStartResult = m_consoleInitResult;
    if (R_FAILED(m_consoleInitResult)) {
        m_consoleRetryTick = now + armNsToTicks(kConsoleRetryDelayNs);
        ++m_startFailureCount;
        return false;
    }
    m_consoleInitialized = true;

    m_consoleStartResult = hidStartSevenSixAxisSensor();
    m_lastStartResult = m_consoleStartResult;
    if (R_FAILED(m_consoleStartResult)) {
        hidFinalizeSevenSixAxisSensor();
        m_consoleInitialized = false;
        m_consoleRetryTick = now + armNsToTicks(kConsoleRetryDelayNs);
        ++m_startFailureCount;
        return false;
    }

    m_consoleStarted = true;
    m_consoleSamplingNumber = 0;
    m_started = true;
    m_source = Source::Console;
    m_handleCount = 0;
    m_activeId = HidNpadIdType_Other;
    m_activeStyle = static_cast<HidNpadStyleTag>(0);
    return true;
}

Result Motion::tryStartController(HidNpadIdType id, HidNpadStyleTag style,
                                  s32 handleCount) {
    HidSixAxisSensorHandle handles[2]{};
    Result rc = hidGetSixAxisSensorHandles(handles, handleCount, id, style);
    if (R_FAILED(rc)) return rc;

    s32 started = 0;
    for (; started < handleCount; ++started) {
        rc = hidStartSixAxisSensor(handles[started]);
        if (R_FAILED(rc)) break;
    }
    if (started != handleCount) {
        while (started > 0) hidStopSixAxisSensor(handles[--started]);
        return rc;
    }

    for (s32 index = 0; index < handleCount; ++index) {
        m_handles[index] = handles[index];
        m_samplingNumbers[index] = 0;
    }
    m_handleCount = handleCount;
    m_started = true;
    return 0;
}

unsigned Motion::readControllerStates(const HidSixAxisSensorHandle *handles,
                                      u64 *samplingNumbers, s32 handleCount,
                                      MotionSample *sample,
                                      u64 *newestSamplingNumber) {
    if (!handles || !samplingNumbers || !sample || !newestSamplingNumber)
        return 0;
    *sample = {};
    *newestSamplingNumber = 0;
    unsigned freshCount = 0;
    for (s32 index = 0; index < handleCount; ++index) {
        HidSixAxisSensorState state{};
        if (hidGetSixAxisSensorStates(handles[index], &state, 1) == 0)
            continue;
        if (!swots::motion_math::isFreshSamplingNumber(
                samplingNumbers[index], state.sampling_number)) continue;

        samplingNumbers[index] = state.sampling_number;
        *newestSamplingNumber =
            std::max(*newestSamplingNumber, state.sampling_number);
        sample->accelX += state.acceleration.x;
        sample->accelY += state.acceleration.y;
        sample->accelZ += state.acceleration.z;
        sample->gyroX += state.angular_velocity.x;
        sample->gyroY += state.angular_velocity.y;
        sample->gyroZ += state.angular_velocity.z;
        sample->attributes |= state.attributes;
        ++freshCount;
    }
    if (freshCount == 0) return 0;

    const float divisor = static_cast<float>(freshCount);
    sample->accelX /= divisor;
    sample->accelY /= divisor;
    sample->accelZ /= divisor;
    sample->gyroX /= divisor;
    sample->gyroY /= divisor;
    sample->gyroZ /= divisor;
    sample->valid = swots::motion_math::hasUsableGravity(
        sample->accelX, sample->accelY, sample->accelZ);
    return freshCount;
}

void Motion::refreshProbeHandles(u64 now) {
    if (now < m_nextProbeRefreshTick) return;
    m_nextProbeRefreshTick = now + armNsToTicks(500'000'000ULL);
    refreshStyleSets();

    const ControllerCandidate *selected = nullptr;
    for (const ControllerCandidate &candidate : kControllerCandidates) {
        const u32 styles = candidate.id == HidNpadIdType_Handheld
                               ? m_handheldStyleSet
                               : m_no1StyleSet;
        if ((styles & static_cast<u32>(candidate.style)) != 0) {
            selected = &candidate;
            break;
        }
    }
    if (!selected) {
        m_probeSource = Source::None;
        m_probeHandleCount = 0;
        return;
    }
    if (m_probeSource == selected->source && m_probeId == selected->id &&
        m_probeStyle == selected->style &&
        m_probeHandleCount == selected->handleCount) return;

    HidSixAxisSensorHandle handles[2]{};
    if (R_FAILED(hidGetSixAxisSensorHandles(
            handles, selected->handleCount, selected->id, selected->style))) {
        m_probeSource = Source::None;
        m_probeHandleCount = 0;
        return;
    }
    for (s32 index = 0; index < selected->handleCount; ++index) {
        m_probeHandles[index] = handles[index];
        m_probeSamplingNumbers[index] = 0;
    }
    m_probeSource = selected->source;
    m_probeId = selected->id;
    m_probeStyle = selected->style;
    m_probeHandleCount = selected->handleCount;
}

bool Motion::probeHighQualityController(u64 now, MotionSample *sample) {
    if (now < m_nextProbeSampleTick) return false;
    m_nextProbeSampleTick = now + armNsToTicks(kPassiveProbeIntervalNs);
    refreshProbeHandles(now);
    if (m_probeHandleCount == 0 || !sample) return false;

    u64 samplingNumber = 0;
    if (readControllerStates(m_probeHandles, m_probeSamplingNumbers,
                             m_probeHandleCount, sample,
                             &samplingNumber) == 0) return false;
    m_probeLastSamplingNumber = samplingNumber;
    return sample->valid &&
           (sample->attributes & HidSixAxisSensorAttribute_IsConnected) != 0 &&
           !swots::motion_math::isInactiveControllerPlaceholder(
               sample->accelX, sample->accelY, sample->accelZ,
               sample->gyroX, sample->gyroY, sample->gyroZ);
}

void Motion::resetInputCalibration() {
    m_state.gravityX = 0.0f;
    m_state.gravityY = 0.0f;
    m_state.gravityZ = 0.0f;
    m_state.lastAccelX = 0.0f;
    m_state.lastAccelY = 0.0f;
    m_state.lastAccelZ = 0.0f;
    m_state.gyroBiasX = 0.0f;
    m_state.gyroBiasY = 0.0f;
    m_state.gyroBiasZ = 0.0f;
    m_state.filteredAccelX = 0.0f;
    m_state.filteredAccelY = 0.0f;
    m_state.sensorStableSeconds = 0.0f;
    m_state.stillSeconds = 0.0f;
    m_state.accelHistoryReady = false;
    m_state.gravityReady = false;
}

void Motion::prepareStreaming(u64 now, bool resetCalibration) {
    m_status = Status::Streaming;
    m_lastFreshSampleTick = now;
    m_lastPhysicalSignalTick = now;
    m_lastReportedSamplingNumber = 0;
    m_lastSample = {};
    if (resetCalibration) resetInputCalibration();
    m_nextRetryTick = 0;
}

bool Motion::switchToController(u64 now) {
    if (m_probeSource == Source::None || m_probeHandleCount == 0) return false;
    const Source targetSource = m_probeSource;
    const HidNpadIdType targetId = m_probeId;
    const HidNpadStyleTag targetStyle = m_probeStyle;
    const s32 targetHandleCount = m_probeHandleCount;

    releaseCurrent();
    m_lastAttemptSource = targetSource;
    m_lastStartResult =
        tryStartController(targetId, targetStyle, targetHandleCount);
    if (R_SUCCEEDED(m_lastStartResult)) {
        m_source = targetSource;
        m_activeId = targetId;
        m_activeStyle = targetStyle;
        prepareStreaming(now, true);
        return true;
    }

    ++m_startFailureCount;
    m_source = Source::None;
    if (tryStartConsole(now)) {
        prepareStreaming(now, true);
    } else {
        tryStartAny(now);
    }
    return false;
}

bool Motion::switchToConsole(u64 now) {
    releaseCurrent();
    m_source = Source::None;
    if (tryStartConsole(now)) {
        prepareStreaming(now, true);
        return true;
    }
    tryStartAny(now);
    return false;
}

Motion::Motion() = default;

Motion::~Motion() {
    suspend();
}

void Motion::resume() {
    if (m_running) return;
    m_running = true;
    m_nextRetryTick = 0;
    tryStartAny(armGetSystemTick());
}

void Motion::suspend() {
    if (!m_running && !m_started && !m_consoleInitialized) return;
    releaseCurrent();
    m_running = false;
    m_source = Source::None;
    m_activeId = HidNpadIdType_Other;
    m_activeStyle = static_cast<HidNpadStyleTag>(0);
    m_status = Status::WaitingRetry;
    m_nextRetryTick = 0;
    m_lastFreshSampleTick = 0;
    m_lastPhysicalSignalTick = 0;
    m_lastReportedSamplingNumber = 0;
    m_lastSample = {};
    m_probeSource = Source::None;
    m_probeHandleCount = 0;
    m_nextProbeRefreshTick = 0;
    m_nextProbeSampleTick = 0;
    m_nextActiveStyleCheckTick = 0;
    m_probeLastSamplingNumber = 0;
    m_sourceSelection = {};
    m_state = {};
}

MotionSample Motion::sample() {
    MotionSample sample{};
    if (!m_running) return sample;
    const u64 now = armGetSystemTick();
    const u64 nowNs = armTicksToNs(now);

    if (m_started && m_source != Source::Console &&
        now >= m_nextActiveStyleCheckTick) {
        m_nextActiveStyleCheckTick =
            now + armNsToTicks(kPassiveProbeIntervalNs);
        refreshStyleSets();
        if (!activeStyleAvailable()) stopAndRetry(now, 0);
    }
    if (!m_started) {
        if (now >= m_nextRetryTick) tryStartAny(now);
        if (!m_started) return sample;
    }

    MotionSample combined{};
    u64 newestSamplingNumber = 0;
    unsigned freshCount = 0;
    if (m_source == Source::Console) {
        HidSevenSixAxisSensorState state{};
        size_t total = 0;
        const Result rc = hidGetSevenSixAxisSensorStates(&state, 1, &total);
        if (R_SUCCEEDED(rc) && total != 0 &&
            swots::motion_math::isFreshSamplingNumber(
                m_consoleSamplingNumber, state.sampling_number)) {
            m_consoleSamplingNumber = state.sampling_number;
            newestSamplingNumber = state.sampling_number;
            // libnx names this payload conservatively, but Nintendo's layout is
            // acceleration xyz, angular velocity xyz, then quaternion xyzw.
            combined.accelX = state.unk_x18[0];
            combined.accelY = state.unk_x18[1];
            combined.accelZ = state.unk_x18[2];
            combined.gyroX = state.unk_x18[3];
            combined.gyroY = state.unk_x18[4];
            combined.gyroZ = state.unk_x18[5];
            combined.attributes = HidSixAxisSensorAttribute_IsConnected;
            freshCount = 1;
        }

        MotionSample controllerSample{};
        const bool highQualityController =
            probeHighQualityController(now, &controllerSample);
        const auto selection = swots::source_selection::update(
            m_sourceSelection, nowNs, false, highQualityController);
        if (selection ==
                swots::source_selection::Event::PreferController &&
            switchToController(now)) {
            combined = controllerSample;
            newestSamplingNumber = m_probeLastSamplingNumber;
            freshCount = controllerSample.valid ? 1U : 0U;
        }
    } else {
        freshCount = readControllerStates(
            m_handles, m_samplingNumbers, m_handleCount, &combined,
            &newestSamplingNumber);
        const bool highQualityController =
            freshCount != 0 && combined.valid &&
            (combined.attributes & HidSixAxisSensorAttribute_IsConnected) != 0 &&
            !swots::motion_math::isInactiveControllerPlaceholder(
                combined.accelX, combined.accelY, combined.accelZ,
                combined.gyroX, combined.gyroY, combined.gyroZ);
        const auto selection = swots::source_selection::update(
            m_sourceSelection, nowNs, true, highQualityController);
        if (selection == swots::source_selection::Event::PreferConsole &&
            switchToConsole(now)) {
            return sample;
        }
    }

    if (freshCount == 0) {
        const u64 staleNs = armTicksToNs(now - m_lastFreshSampleTick);
        if (staleNs >= 250'000'000ULL) m_status = Status::NoSamples;
        if (staleNs >= kRetryDelayNs) {
            const bool consoleFailed = m_source == Source::Console;
            if (consoleFailed)
                m_consoleRetryTick = now + armNsToTicks(kConsoleRetryDelayNs);
            stopAndRetry(now, consoleFailed ? 0 : kRetryDelayNs);
        }
        if (m_lastSample.valid &&
            swots::motion_math::shouldHoldLastSample(staleNs)) {
            return m_lastSample;
        }
        return sample;
    }

    m_lastFreshSampleTick = now;
    m_lastReportedSamplingNumber = newestSamplingNumber;

    const bool physicalSignal = swots::motion_math::hasUsableGravity(
        combined.accelX, combined.accelY, combined.accelZ);
    if (physicalSignal) m_lastPhysicalSignalTick = now;
    const u64 zeroDataNs = armTicksToNs(now - m_lastPhysicalSignalTick);
    if (!physicalSignal && zeroDataNs >= 250'000'000ULL)
        m_status = Status::ZeroData;
    if (!physicalSignal && zeroDataNs >= kRetryDelayNs) {
        const bool consoleFailed = m_source == Source::Console;
        if (consoleFailed)
            m_consoleRetryTick = now + armNsToTicks(kConsoleRetryDelayNs);
        m_lastSample = combined;
        stopAndRetry(now, consoleFailed ? 0 : kRetryDelayNs);
        return sample;
    }

    combined.valid = physicalSignal;
    if (physicalSignal || zeroDataNs < 250'000'000ULL)
        m_status = Status::Streaming;
    m_lastSample = combined;
    return combined;
}

void Motion::update(const MotionSample &sample, const Config &config, float dt) {
    swots::motion_math::Input input{
        sample.accelX, sample.accelY, sample.accelZ,
        sample.gyroX, sample.gyroY, sample.gyroZ, sample.valid};
    swots::motion_math::Parameters parameters{config.sensitivity,
                                               config.smoothing};
    swots::motion_math::update(m_state, input, parameters, dt);
}

bool Motion::tryStartAny(u64 now) {
    refreshStyleSets();
    m_source = Source::None;
    m_lastAttemptSource = Source::None;
    m_lastStartResult = MAKERESULT(Module_Libnx, LibnxError_NotFound);

    if (!tryStartConsole(now)) {
        for (const ControllerCandidate &candidate : kControllerCandidates) {
            const u32 styles = candidate.id == HidNpadIdType_Handheld
                                   ? m_handheldStyleSet
                                   : m_no1StyleSet;
            if ((styles & static_cast<u32>(candidate.style)) == 0) continue;
            m_lastAttemptSource = candidate.source;
            m_lastStartResult = tryStartController(
                candidate.id, candidate.style, candidate.handleCount);
            if (R_SUCCEEDED(m_lastStartResult)) {
                m_source = candidate.source;
                m_activeId = candidate.id;
                m_activeStyle = candidate.style;
                break;
            }
            ++m_startFailureCount;
        }
    }

    if (!m_started) {
        m_status = Status::WaitingRetry;
        m_nextRetryTick = now + armNsToTicks(kRetryDelayNs);
        return false;
    }
    prepareStreaming(now, true);
    return true;
}

void Motion::releaseCurrent() {
    if (m_source == Source::Console) {
        if (m_consoleStarted) hidStopSevenSixAxisSensor();
        if (m_consoleInitialized) hidFinalizeSevenSixAxisSensor();
    } else if (m_started) {
        for (s32 index = 0; index < m_handleCount; ++index)
            hidStopSixAxisSensor(m_handles[index]);
    }
    m_consoleStarted = false;
    m_consoleInitialized = false;
    m_started = false;
    m_handleCount = 0;
}

void Motion::stopAndRetry(u64 now, u64 delayNs) {
    releaseCurrent();
    m_source = Source::None;
    m_activeId = HidNpadIdType_Other;
    m_activeStyle = static_cast<HidNpadStyleTag>(0);
    m_status = Status::WaitingRetry;
    m_lastFreshSampleTick = 0;
    m_lastPhysicalSignalTick = 0;
    m_nextRetryTick = now + armNsToTicks(delayNs);
    ++m_retryCount;
}

void Motion::refreshStyleSets() {
    m_no1StyleSet = hidGetNpadStyleSet(HidNpadIdType_No1);
    m_handheldStyleSet = hidGetNpadStyleSet(HidNpadIdType_Handheld);
}

bool Motion::activeStyleAvailable() const {
    if (!m_started) return false;
    if (m_source == Source::Console) return true;
    const u32 styles = m_activeId == HidNpadIdType_Handheld
                           ? m_handheldStyleSet
                           : m_no1StyleSet;
    return (styles & static_cast<u32>(m_activeStyle)) != 0;
}

u64 Motion::freshAgeMs(u64 now) const {
    if (m_lastFreshSampleTick == 0 || now < m_lastFreshSampleTick) return 0;
    return armTicksToNs(now - m_lastFreshSampleTick) / 1'000'000ULL;
}

u64 Motion::signalAgeMs(u64 now) const {
    if (m_lastPhysicalSignalTick == 0 || now < m_lastPhysicalSignalTick) return 0;
    return armTicksToNs(now - m_lastPhysicalSignalTick) / 1'000'000ULL;
}

const char *Motion::lastAttemptName() const {
    switch (m_lastAttemptSource) {
        case Source::Console: return "Console";
        case Source::Handheld: return "Handheld";
        case Source::FullKey: return "FullKey";
        case Source::JoyDual: return "JoyDual";
        default: return "None";
    }
}

const char *Motion::sourceName() const {
    switch (m_source) {
        case Source::Console: return "Console";
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
        case Status::ZeroData: return "ZeroData";
        default: return "WaitingRetry";
    }
}
