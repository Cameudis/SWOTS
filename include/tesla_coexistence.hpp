// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>

#include "tesla_lifecycle.hpp"

// Pure state machine shared by the Switch renderer and host-side tests.
// Ordered frontend lifecycle records are authoritative. The shortcut remains
// a conservative fallback for opening SWOTS and closing the parent menu.
namespace tesla_coexistence {

inline constexpr std::uint64_t ResumeCooldownNs = 500'000'000ULL;

enum class Event {
    None,
    Suspended,
    Closing,
    Disabled,
};

using LifecycleState = tesla_lifecycle::State;

struct LifecycleSignal {
    bool present = false;
    std::uint64_t session = 0;
    std::uint64_t generation = 0;
    LifecycleState state = LifecycleState::Visible;
};

struct Decision {
    bool allowLayer = false;
    bool destroyLayer = false;
    bool acknowledgeLifecycle = false;
    std::uint64_t lifecycleSession = 0;
    std::uint64_t lifecycleGeneration = 0;
    Event event = Event::None;
};

class StateMachine {
public:
    Decision update(bool enabled, bool comboHeld,
                    const LifecycleSignal &lifecycle,
                    std::uint64_t nowNs) {
        const bool comboPressed = comboHeld && !m_comboWasHeld;
        m_comboWasHeld = comboHeld;

        if (!enabled) {
            const bool stateChanged = m_enabled || m_mode != Mode::Unknown ||
                                      m_resumeNotBeforeNs != 0;
            m_enabled = false;
            m_mode = Mode::Unknown;
            m_resumeNotBeforeNs = 0;
            m_hasLifecycle = false;
            Decision disabled{};
            disabled.destroyLayer = true;
            disabled.event = stateChanged ? Event::Disabled : Event::None;
            return disabled;
        }

        m_enabled = true;

        Decision result{};
        bool acceptedLifecycle = false;
        LifecycleState acceptedState = LifecycleState::Visible;
        const bool sameSession =
            m_hasLifecycle && lifecycle.session == m_lifecycleSession;
        const bool newerInSession =
            sameSession && lifecycle.generation > m_lifecycleGeneration;
        const bool safeNewSession =
            !sameSession && lifecycle.state != LifecycleState::Hidden;
        if (lifecycle.present && lifecycle.session != 0 &&
            lifecycle.generation != 0 &&
            ((!m_hasLifecycle && lifecycle.state != LifecycleState::Hidden) ||
             newerInSession || (m_hasLifecycle && safeNewSession))) {
            m_hasLifecycle = true;
            m_lifecycleSession = lifecycle.session;
            m_lifecycleGeneration = lifecycle.generation;
            acceptedLifecycle = true;
            acceptedState = lifecycle.state;

            if (acceptedState == LifecycleState::Visible) {
                result.acknowledgeLifecycle = true;
                result.lifecycleSession = lifecycle.session;
                result.lifecycleGeneration = lifecycle.generation;
            }

            if (acceptedState == LifecycleState::Hidden) {
                m_mode = Mode::Rendering;
                m_resumeNotBeforeNs = nowNs + ResumeCooldownNs;
                result.event = Event::Closing;
            } else {
                m_mode = acceptedState == LifecycleState::Parent
                             ? Mode::ParentVisible
                             : Mode::SwotsVisible;
                m_resumeNotBeforeNs = 0;
                result.destroyLayer = true;
                result.event = Event::Suspended;
            }
        }

        if (comboPressed && !acceptedLifecycle) {
            if (m_mode == Mode::Rendering || m_mode == Mode::Unknown) {
                m_mode = Mode::SwotsVisible;
                m_resumeNotBeforeNs = 0;
                result.destroyLayer = true;
                result.event = Event::Suspended;
            }
        }

        if (acceptedLifecycle) result.destroyLayer = true;
        if (m_mode != Mode::Rendering) result.destroyLayer = true;
        result.allowLayer = m_mode == Mode::Rendering && !comboHeld &&
                            nowNs >= m_resumeNotBeforeNs;
        return result;
    }

    bool suspended() const { return m_mode != Mode::Rendering; }
    std::uint64_t resumeNotBeforeNs() const { return m_resumeNotBeforeNs; }

private:
    enum class Mode {
        Unknown,
        Rendering,
        SwotsVisible,
        ParentVisible,
    };

    bool m_enabled = false;
    bool m_comboWasHeld = false;
    bool m_hasLifecycle = false;
    Mode m_mode = Mode::Unknown;
    std::uint64_t m_lifecycleSession = 0;
    std::uint64_t m_lifecycleGeneration = 0;
    std::uint64_t m_resumeNotBeforeNs = 0;
};

} // namespace tesla_coexistence
