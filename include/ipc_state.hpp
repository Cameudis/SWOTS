// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "ipc_protocol.hpp"

#include <cstdint>

namespace swots::ipc {

enum class StateResult : std::uint32_t {
    Success = 0,
    Invalid = 1,
    NotOwner = 2,
    Busy = 3,
    Stale = 4,
    Conflict = 5,
};

struct SessionIdentity {
    std::uint8_t slot = 0;
    std::uint32_t generation = 0;

    friend constexpr bool operator==(SessionIdentity,
                                     SessionIdentity) = default;
};

struct RenderWork {
    DesiredState desiredState = DesiredState::Suspended;
    SessionIdentity owner{};
    std::uint64_t ownerEpoch = 0;
    std::uint64_t lifecycleSequence = 0;
    std::uint64_t configRevision = 0;
    bool configValid = false;
    SettingsWire settings{};
};

class StateMachine {
public:
    StateResult claimAndSuspend(SessionIdentity caller,
                                const OwnerCommand &command) noexcept {
        if (!validIdentity(caller) || command.ownerEpoch == 0 ||
            command.lifecycleSequence == 0 ||
            m_processState == ProcessState::Stopping ||
            m_processState == ProcessState::Fault)
            return StateResult::Invalid;
        if (m_hasOwner && caller != m_owner) return StateResult::Busy;
        if (m_hasOwner && command.ownerEpoch != m_ownerEpoch)
            return StateResult::Conflict;
        if (m_hasOwner && command.lifecycleSequence < m_lifecycleSequence)
            return StateResult::Stale;
        if (m_hasOwner && command.lifecycleSequence == m_lifecycleSequence &&
            m_desiredState != DesiredState::Suspended)
            return StateResult::Conflict;

        const bool ownershipTransfer = !m_hasOwner;
        m_hasOwner = true;
        m_owner = caller;
        m_ownerEpoch = command.ownerEpoch;
        m_ownerReady = false;
        if (ownershipTransfer) {
            m_appliedLifecycleSequence = 0;
            m_acceptedConfigRevision = 0;
            m_appliedConfigRevision = 0;
            m_acceptedConfigValid = false;
            m_appliedConfigValid = false;
        }
        m_lifecycleSequence = command.lifecycleSequence;
        m_desiredState = DesiredState::Suspended;
        if (m_processState == ProcessState::Running)
            m_processState = ProcessState::Suspending;
        return StateResult::Success;
    }

    StateResult setConfig(SessionIdentity caller,
                          const ConfigCommand &command) noexcept {
        if (!authorize(caller, command.ownerEpoch)) return StateResult::NotOwner;
        if (!validSettings(command.settings))
            return StateResult::Invalid;
        if (m_acceptedConfigValid &&
            command.configRevision < m_acceptedConfigRevision)
            return StateResult::Stale;
        if (m_acceptedConfigValid &&
            command.configRevision == m_acceptedConfigRevision) {
            return sameSettings(command.settings, m_settings)
                       ? StateResult::Success
                       : StateResult::Conflict;
        }
        m_acceptedConfigRevision = command.configRevision;
        m_acceptedConfigValid = true;
        m_settings = command.settings;
        return StateResult::Success;
    }

    StateResult resume(SessionIdentity caller,
                       const ResumeCommand &command) noexcept {
        if (!authorize(caller, command.ownerEpoch)) return StateResult::NotOwner;
        if (!validSettings(command.settings) ||
            command.lifecycleSequence == 0)
            return StateResult::Invalid;
        if (command.lifecycleSequence < m_lifecycleSequence ||
            (m_acceptedConfigValid &&
             command.configRevision < m_acceptedConfigRevision))
            return StateResult::Stale;
        if (command.lifecycleSequence == m_lifecycleSequence) {
            if (m_desiredState != DesiredState::Running ||
                !m_acceptedConfigValid ||
                command.configRevision != m_acceptedConfigRevision ||
                !sameSettings(command.settings, m_settings))
                return StateResult::Conflict;
            return StateResult::Success;
        }
        if (m_acceptedConfigValid &&
            command.configRevision == m_acceptedConfigRevision &&
            !sameSettings(command.settings, m_settings))
            return StateResult::Conflict;

        m_lifecycleSequence = command.lifecycleSequence;
        m_acceptedConfigRevision = command.configRevision;
        m_acceptedConfigValid = true;
        m_settings = command.settings;
        m_desiredState = DesiredState::Running;
        return StateResult::Success;
    }

    StateResult requestStop(SessionIdentity caller,
                            const OwnerCommand &command) noexcept {
        if (!authorize(caller, command.ownerEpoch)) return StateResult::NotOwner;
        if (command.lifecycleSequence == 0) return StateResult::Invalid;
        if (command.lifecycleSequence < m_lifecycleSequence)
            return StateResult::Stale;
        if (command.lifecycleSequence == m_lifecycleSequence &&
            m_desiredState != DesiredState::Stopping)
            return StateResult::Conflict;
        m_lifecycleSequence = command.lifecycleSequence;
        m_desiredState = DesiredState::Stopping;
        m_processState = ProcessState::Stopping;
        return StateResult::Success;
    }

    void disconnect(SessionIdentity caller) noexcept {
        if (!m_hasOwner || caller != m_owner) return;
        m_hasOwner = false;
        m_ownerReady = false;
        m_desiredState = DesiredState::Suspended;
        if (m_processState == ProcessState::Running)
            m_processState = ProcessState::Suspending;
    }

    bool workStillOwned(const RenderWork &work) const noexcept {
        return m_hasOwner && m_ownerReady && work.owner == m_owner &&
               work.ownerEpoch == m_ownerEpoch;
    }

    bool canPublishRunning(const RenderWork &work) const noexcept {
        return workStillOwned(work) &&
               m_desiredState == DesiredState::Running &&
               work.lifecycleSequence == m_lifecycleSequence &&
               m_acceptedConfigValid && work.configValid &&
               work.configRevision == m_acceptedConfigRevision &&
               sameSettings(work.settings, m_settings);
    }

    RenderWork work() const noexcept {
        return {m_desiredState, m_owner, m_ownerEpoch, m_lifecycleSequence,
                m_acceptedConfigRevision, m_acceptedConfigValid, m_settings};
    }

    void publishSuspended(const RenderWork &work,
                          std::uint64_t revision,
                          bool configValid) noexcept {
        m_processState = ProcessState::Suspended;
        m_appliedState = DesiredState::Suspended;
        const bool currentOwner =
            m_hasOwner && work.owner == m_owner &&
            work.ownerEpoch == m_ownerEpoch &&
            work.lifecycleSequence == m_lifecycleSequence;
        if (!currentOwner) return;
        m_appliedLifecycleSequence = m_lifecycleSequence;
        m_ownerReady = true;
        if (configValid && work.configValid && m_acceptedConfigValid &&
            work.configRevision == m_acceptedConfigRevision &&
            sameSettings(work.settings, m_settings)) {
            m_appliedConfigRevision = revision;
            m_appliedConfigValid = true;
        }
    }

    void publishRunning(std::uint64_t sequence,
                        std::uint64_t revision) noexcept {
        m_processState = ProcessState::Running;
        m_appliedState = DesiredState::Running;
        m_appliedLifecycleSequence = sequence;
        m_appliedConfigRevision = revision;
        m_appliedConfigValid = true;
    }

    void publishFault(std::uint32_t result) noexcept {
        m_processState = ProcessState::Fault;
        m_lastResult = result;
    }

    StatusWire status(SessionIdentity caller) const noexcept {
        StatusWire status{};
        status.processState = m_processState;
        status.desiredState = m_desiredState;
        status.appliedState = m_appliedState;
        status.callerIsOwner = m_hasOwner && m_ownerReady && caller == m_owner;
        status.ownerEpoch = m_ownerEpoch;
        status.appliedLifecycleSequence = m_appliedLifecycleSequence;
        status.acceptedConfigRevision = m_acceptedConfigRevision;
        status.appliedConfigRevision = m_appliedConfigRevision;
        status.lastResult = m_lastResult;
        status.acceptedConfigValid = m_acceptedConfigValid;
        status.appliedConfigValid = m_appliedConfigValid;
        return status;
    }

    bool hasOwner() const noexcept { return m_hasOwner; }
    bool ownerReady() const noexcept { return m_hasOwner && m_ownerReady; }
    bool sessionIsOwner(SessionIdentity caller) const noexcept {
        return m_hasOwner && caller == m_owner;
    }
    ProcessState processState() const noexcept { return m_processState; }
    DesiredState desiredState() const noexcept { return m_desiredState; }

private:
    static constexpr bool validIdentity(SessionIdentity identity) noexcept {
        return identity.generation != 0;
    }

    bool authorize(SessionIdentity caller, std::uint64_t epoch) const noexcept {
        return m_hasOwner && m_ownerReady && caller == m_owner && epoch != 0 &&
               epoch == m_ownerEpoch;
    }

    static bool sameSettings(const SettingsWire &a,
                             const SettingsWire &b) noexcept {
        return a.size == b.size && a.version == b.version &&
               a.opacity == b.opacity && a.dotRadius == b.dotRadius &&
               a.sensitivity == b.sensitivity && a.smoothing == b.smoothing;
    }

    bool m_hasOwner = false;
    bool m_ownerReady = false;
    SessionIdentity m_owner{};
    std::uint64_t m_ownerEpoch = 0;
    std::uint64_t m_lifecycleSequence = 0;
    std::uint64_t m_acceptedConfigRevision = 0;
    bool m_acceptedConfigValid = false;
    std::uint64_t m_appliedLifecycleSequence = 0;
    std::uint64_t m_appliedConfigRevision = 0;
    bool m_appliedConfigValid = false;
    SettingsWire m_settings{};
    ProcessState m_processState = ProcessState::Starting;
    DesiredState m_desiredState = DesiredState::Suspended;
    DesiredState m_appliedState = DesiredState::Suspended;
    std::uint32_t m_lastResult = 0;
};

} // namespace swots::ipc
