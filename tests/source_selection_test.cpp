// SPDX-License-Identifier: GPL-2.0-or-later

#include "source_selection.hpp"

#include <cstdio>

using swots::source_selection::Event;
using swots::source_selection::State;

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", \
    __FILE__, __LINE__, #x); ++failures; } } while (false)

void stableControllerUpgradesOnce() {
    State state;
    CHECK(update(state, 1, false, true) == Event::None);
    for (std::uint64_t now = 100'000'000; now <= 500'000'000;
         now += 100'000'000) {
        CHECK(update(state, now, false, true) == Event::None);
    }
    CHECK(update(state, 600'000'001, false, true) == Event::PreferController);
    CHECK(update(state, 700'000'000, true, true) == Event::None);
}

void intermittentProbeDoesNotUpgrade() {
    State state;
    CHECK(update(state, 1, false, true) == Event::None);
    CHECK(update(state, 300'000'000, false, false) == Event::None);
    CHECK(update(state, 400'000'000, false, true) == Event::None);
    CHECK(update(state, 900'000'000, false, true) == Event::None);
}

void shortProbeGapIsTolerated() {
    State state;
    CHECK(update(state, 1, false, true) == Event::None);
    for (std::uint64_t now = 100'000'000; now <= 500'000'000;
         now += 100'000'000) {
        CHECK(update(state, now, false, true) == Event::None);
    }
    CHECK(update(state, 550'000'000, false, false) == Event::None);
    CHECK(update(state, 600'000'001, false, true) == Event::PreferController);
}

void sustainedLossFallsBack() {
    State state;
    CHECK(update(state, 1, true, false) == Event::None);
    CHECK(update(state, 800'000'000, true, false) == Event::None);
    CHECK(update(state, 800'000'001, true, false) == Event::PreferConsole);
}

void recoveryCancelsFallback() {
    State state;
    CHECK(update(state, 1, true, false) == Event::None);
    CHECK(update(state, 500'000'000, true, true) == Event::None);
    CHECK(update(state, 1'000'000'000, true, false) == Event::None);
    CHECK(update(state, 1'700'000'000, true, false) == Event::None);
}

void cooldownPreventsFlapping() {
    State state;
    CHECK(update(state, 1, false, true) == Event::None);
    for (std::uint64_t now = 100'000'000; now <= 500'000'000;
         now += 100'000'000) {
        CHECK(update(state, now, false, true) == Event::None);
    }
    CHECK(update(state, 600'000'001, false, true) == Event::PreferController);
    CHECK(update(state, 700'000'000, true, false) == Event::None);
    CHECK(update(state, 2'000'000'000, true, false) == Event::None);
    CHECK(update(state, 3'600'000'001, true, false) == Event::PreferConsole);
}
} // namespace

int main() {
    stableControllerUpgradesOnce();
    intermittentProbeDoesNotUpgrade();
    shortProbeGapIsTolerated();
    sustainedLossFallsBack();
    recoveryCancelsFallback();
    cooldownPreventsFlapping();
    if (failures) return 1;
    std::puts("source selection tests passed");
    return 0;
}
