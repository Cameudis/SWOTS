// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <cstdint>

namespace tesla_lifecycle {

enum class ExitIntent : std::uint8_t {
    None,
    ResumeGame,
    ReturnParent,
};

class ExitIntentState {
public:
    void reset() { m_intent.store(ExitIntent::None, std::memory_order_release); }

    void requestResumeGame() {
        ExitIntent expected = ExitIntent::None;
        m_intent.compare_exchange_strong(expected, ExitIntent::ResumeGame,
                                         std::memory_order_acq_rel);
    }

    void requestReturnParent() {
        m_intent.store(ExitIntent::ReturnParent, std::memory_order_release);
    }

    ExitIntent current() const {
        return m_intent.load(std::memory_order_acquire);
    }

    ExitIntent consume() {
        return m_intent.exchange(ExitIntent::None, std::memory_order_acq_rel);
    }

private:
    std::atomic<ExitIntent> m_intent = ExitIntent::None;
};

} // namespace tesla_lifecycle
