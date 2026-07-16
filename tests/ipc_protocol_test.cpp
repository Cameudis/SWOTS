// SPDX-License-Identifier: GPL-2.0-or-later

#include "ipc_state.hpp"

#include <cassert>
#include <iostream>

using namespace swots::ipc;

int main() {
    ProtocolInfo info{};
    assert(compatible(info));
    info.abiMajor++;
    assert(!compatible(info));
    info = {};
    info.rendererVersion++;
    assert(!compatible(info));

    StateMachine state;
    const SessionIdentity owner{0, 1};
    const SessionIdentity stranger{1, 1};
    assert(state.claimAndSuspend(owner, {7, 1}) == StateResult::Success);
    assert(state.status(owner).callerIsOwner == 0);
    state.publishSuspended(state.work(), 0, false);
    assert(state.status(owner).callerIsOwner == 1);
    assert(state.claimAndSuspend(stranger, {7, 2}) == StateResult::Busy);

    ConfigCommand config{};
    config.ownerEpoch = 7;
    config.configRevision = 1;
    assert(state.setConfig(stranger, config) == StateResult::NotOwner);
    assert(state.setConfig(owner, config) == StateResult::Success);
    config.settings.opacity = 99;
    assert(state.setConfig(owner, config) == StateResult::Conflict);

    ResumeCommand resume{};
    resume.ownerEpoch = 7;
    resume.lifecycleSequence = 2;
    resume.configRevision = 1;
    assert(state.resume(owner, resume) == StateResult::Success);
    assert(state.claimAndSuspend(owner, {7, 2}) == StateResult::Conflict);
    resume.settings.opacity = 99;
    assert(state.resume(owner, resume) == StateResult::Conflict);
    const RenderWork work = state.work();
    assert(state.canPublishRunning(work));
    assert(state.workStillOwned(work));
    state.disconnect(owner);
    assert(!state.canPublishRunning(work));
    assert(!state.workStillOwned(work));
    assert(state.desiredState() == DesiredState::Suspended);

    assert(state.claimAndSuspend(stranger, {11, 3}) == StateResult::Success);
    assert(state.status(stranger).callerIsOwner == 0);
    state.publishSuspended(work, 1, true);
    assert(state.status(stranger).callerIsOwner == 0);
    state.publishSuspended(state.work(), 1, false);
    assert(state.status(stranger).callerIsOwner == 1);
    assert(state.status(owner).callerIsOwner == 0);
    ConfigCommand newOwnerConfig{};
    newOwnerConfig.ownerEpoch = 11;
    newOwnerConfig.configRevision = 0;
    assert(state.setConfig(stranger, newOwnerConfig) == StateResult::Success);

    SettingsWire invalid{};
    invalid.reserved[0] = 1;
    assert(!validSettings(invalid));

    StateMachine legacy;
    assert(legacy.claimAndSuspend(owner, {9, 1}) == StateResult::Success);
    legacy.publishSuspended(legacy.work(), 0, false);
    ConfigCommand revisionZero{};
    revisionZero.ownerEpoch = 9;
    revisionZero.configRevision = 0;
    assert(legacy.setConfig(owner, revisionZero) == StateResult::Success);
    const RenderWork legacyWork = legacy.work();
    legacy.publishSuspended(legacyWork, 0, true);
    assert(legacy.status(owner).appliedConfigValid == 1);
    assert(legacy.status(owner).appliedConfigRevision == 0);

    std::cout << "ipc protocol tests passed\n";
}
