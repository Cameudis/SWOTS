// SPDX-License-Identifier: GPL-2.0-or-later

#include "tesla_coexistence.hpp"
#include "tesla_exit_intent.hpp"

#include <cstdio>

using tesla_coexistence::Event;
using tesla_coexistence::LifecycleSignal;
using tesla_coexistence::LifecycleState;
using tesla_coexistence::ResumeCooldownNs;
using tesla_coexistence::StateMachine;

namespace {

int failures = 0;

#define CHECK(expression)                                                       \
    do {                                                                        \
        if (!(expression)) {                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,       \
                         __LINE__, #expression);                                \
            ++failures;                                                         \
        }                                                                       \
    } while (false)

LifecycleSignal signal(std::uint64_t session, std::uint64_t generation,
                       LifecycleState state) {
    return {true, session, generation, state};
}

LifecycleSignal noSignal() { return {}; }

void backendStartsSafelyPaused() {
    StateMachine state;

    auto initial = state.update(true, false, noSignal(), 0);
    CHECK(!initial.allowLayer);
    CHECK(initial.destroyLayer);
    CHECK(state.suspended());

    // Time alone cannot prove that Tesla released the foreground.
    CHECK(!state.update(true, false, noSignal(),
                        10 * ResumeCooldownNs).allowLayer);
}

void visibleAlwaysDestroysTheLayer() {
    StateMachine state;
    state.update(true, false, signal(1, 1, LifecycleState::Visible), 0);
    state.update(true, false, signal(1, 2, LifecycleState::Hidden), 1);
    CHECK(state.update(true, false, noSignal(), ResumeCooldownNs + 1).allowLayer);

    auto visible = state.update(true, false,
                                signal(1, 3, LifecycleState::Visible),
                                ResumeCooldownNs + 1);
    CHECK(visible.event == Event::Suspended);
    CHECK(visible.destroyLayer);
    CHECK(!visible.allowLayer);
    CHECK(state.suspended());
    CHECK(visible.acknowledgeLifecycle);
    CHECK(visible.lifecycleSession == 1);
    CHECK(visible.lifecycleGeneration == 3);
}

void hiddenFromAnUnownedSessionIsRejected() {
    StateMachine state;
    auto hidden = state.update(true, false,
                               signal(99, 1, LifecycleState::Hidden), 0);
    CHECK(hidden.event == Event::None);
    CHECK(!hidden.allowLayer);
    CHECK(state.suspended());
}

void hiddenResumesOnlyAfterCooldown() {
    StateMachine state;
    state.update(true, false, signal(2, 10, LifecycleState::Visible), 0);

    const std::uint64_t hiddenAt = 100;
    auto hidden = state.update(true, false,
                               signal(2, 11, LifecycleState::Hidden), hiddenAt);
    CHECK(hidden.event == Event::Closing);
    CHECK(hidden.destroyLayer);
    CHECK(!hidden.allowLayer);
    CHECK(!state.suspended());
    CHECK(!state.update(true, false, noSignal(),
                        hiddenAt + ResumeCooldownNs - 1).allowLayer);
    CHECK(state.update(true, false, noSignal(),
                       hiddenAt + ResumeCooldownNs).allowLayer);
}

void parentAlwaysStaysPaused() {
    StateMachine state;
    state.update(true, false, signal(3, 1, LifecycleState::Visible), 0);
    auto parent = state.update(true, false,
                               signal(3, 2, LifecycleState::Parent), 10);
    CHECK(parent.destroyLayer);
    CHECK(!parent.allowLayer);
    CHECK(state.suspended());
    CHECK(!state.update(true, false, noSignal(),
                        10 * ResumeCooldownNs).allowLayer);

    const std::uint64_t comboAt = 10 * ResumeCooldownNs + 1;
    auto combo = state.update(true, true, noSignal(), comboAt);
    CHECK(combo.event == Event::None);
    CHECK(!combo.allowLayer);
    CHECK(state.suspended());
    state.update(true, false, noSignal(), comboAt + 1);
    CHECK(!state.update(true, false, noSignal(),
                        comboAt + 2 * ResumeCooldownNs).allowLayer);
}

void oldGenerationInSameSessionIsIgnored() {
    StateMachine state;
    state.update(true, false, signal(4, 8, LifecycleState::Visible), 0);

    auto staleHidden = state.update(
        true, false, signal(4, 7, LifecycleState::Hidden), ResumeCooldownNs);
    CHECK(staleHidden.event == Event::None);
    CHECK(!staleHidden.allowLayer);
    CHECK(state.suspended());

    // Replaying the current generation is also idempotent.
    auto duplicate = state.update(
        true, false, signal(4, 8, LifecycleState::Hidden), 2 * ResumeCooldownNs);
    CHECK(duplicate.event == Event::None);
    CHECK(!duplicate.allowLayer);
    CHECK(state.suspended());

    auto freshHidden = state.update(
        true, false, signal(4, 9, LifecycleState::Hidden), 2 * ResumeCooldownNs);
    CHECK(freshHidden.event == Event::Closing);
    CHECK(!state.suspended());
}

void comboOpeningCancelsARecentHiddenResume() {
    StateMachine state;
    const std::uint64_t hiddenAt = 100;
    state.update(true, false, signal(5, 1, LifecycleState::Visible), 0);
    state.update(true, false, signal(5, 2, LifecycleState::Hidden), hiddenAt);

    auto opened = state.update(true, true, noSignal(), hiddenAt + 1);
    CHECK(opened.event == Event::Suspended);
    CHECK(opened.destroyLayer);
    CHECK(!opened.allowLayer);
    CHECK(state.suspended());

    state.update(true, false, noSignal(), hiddenAt + 2);
    CHECK(!state.update(true, false, noSignal(),
                        hiddenAt + 2 * ResumeCooldownNs).allowLayer);
}

void lifecycleWinsOverComboInTheSameFrame() {
    StateMachine state;
    state.update(true, false, signal(9, 1, LifecycleState::Visible), 0);

    const std::uint64_t hiddenAt = 100;
    auto hidden = state.update(true, true,
                               signal(9, 2, LifecycleState::Hidden), hiddenAt);
    CHECK(hidden.event == Event::Closing);
    CHECK(!state.suspended());
    state.update(true, false, noSignal(), hiddenAt + 1);
    CHECK(state.update(true, false, noSignal(),
                       hiddenAt + ResumeCooldownNs).allowLayer);

    state.update(true, true, signal(9, 3, LifecycleState::Parent),
                 hiddenAt + ResumeCooldownNs + 1);
    CHECK(state.suspended());
    state.update(true, false, noSignal(), hiddenAt + ResumeCooldownNs + 2);
    CHECK(!state.update(true, false, noSignal(),
                        hiddenAt + 3 * ResumeCooldownNs).allowLayer);
}

void visibleWinsOverComboInTheSameFrame() {
    StateMachine state;
    state.update(true, false, signal(6, 1, LifecycleState::Parent), 0);

    // From Parent, the shortcut alone means close. A newer Visible record in
    // the same poll is authoritative and must prevent an unsafe resume.
    auto decision = state.update(true, true,
                                 signal(6, 2, LifecycleState::Visible), 10);
    CHECK(decision.event == Event::Suspended);
    CHECK(decision.destroyLayer);
    CHECK(!decision.allowLayer);
    CHECK(state.suspended());
    CHECK(!state.update(true, false, noSignal(),
                        2 * ResumeCooldownNs).allowLayer);
}

void disableAndReenableRequiresFreshLifecycleState() {
    StateMachine state;
    state.update(true, false, signal(7, 1, LifecycleState::Visible), 0);
    state.update(true, false, signal(7, 2, LifecycleState::Hidden), 1);
    CHECK(state.update(true, false, noSignal(), ResumeCooldownNs + 1).allowLayer);

    auto disabled = state.update(false, false, noSignal(), ResumeCooldownNs + 1);
    CHECK(disabled.event == Event::Disabled);
    CHECK(disabled.destroyLayer);
    CHECK(!disabled.allowLayer);

    auto reenabled = state.update(true, false, noSignal(), ResumeCooldownNs + 2);
    CHECK(reenabled.destroyLayer);
    CHECK(!reenabled.allowLayer);
    CHECK(state.suspended());

    state.update(true, false, signal(8, 1, LifecycleState::Visible),
                 ResumeCooldownNs + 3);
    const std::uint64_t hiddenAt = ResumeCooldownNs + 4;
    state.update(true, false, signal(8, 2, LifecycleState::Hidden), hiddenAt);
    CHECK(state.update(true, false, noSignal(),
                       hiddenAt + ResumeCooldownNs).allowLayer);
}

void returnParentWinsOverSameFrameResumeRequest() {
    tesla_lifecycle::ExitIntentState intent;
    intent.requestReturnParent();
    intent.requestResumeGame();
    CHECK(intent.current() == tesla_lifecycle::ExitIntent::ReturnParent);
    CHECK(intent.consume() == tesla_lifecycle::ExitIntent::ReturnParent);
    CHECK(intent.current() == tesla_lifecycle::ExitIntent::None);
}

} // namespace

int main() {
    backendStartsSafelyPaused();
    visibleAlwaysDestroysTheLayer();
    hiddenFromAnUnownedSessionIsRejected();
    hiddenResumesOnlyAfterCooldown();
    parentAlwaysStaysPaused();
    oldGenerationInSameSessionIsIgnored();
    comboOpeningCancelsARecentHiddenResume();
    lifecycleWinsOverComboInTheSameFrame();
    visibleWinsOverComboInTheSameFrame();
    disableAndReenableRequiresFreshLifecycleState();
    returnParentWinsOverSameFrameResumeRequest();

    if (failures != 0) {
        std::fprintf(stderr, "%d test assertion(s) failed\n", failures);
        return 1;
    }
    std::puts("tesla coexistence tests passed");
    return 0;
}
