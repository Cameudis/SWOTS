// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <switch.h>
#include "config.hpp"
#include "motion.hpp"

namespace overlay {

struct Context {
    ViDisplay display{};
    ViLayer layer{};
    NWindow window{};
    Framebuffer framebuffer{};
    Motion::Source observedSource = Motion::Source::None;
    Motion::Source toastSource = Motion::Source::None;
    float toastRemaining = 0.0f;
    float observedOffsetX = 0.0f;
    float observedOffsetY = 0.0f;
    float observedRoll = 0.0f;
    bool hasObservedVisual = false;
    bool initialized = false;
};

struct FrameState {
    bool visualChanged = false;
    bool toastActive = false;
};

Result init(Context *context);
FrameState update(Context *context, Motion &motion, const Config &config,
                  float dt);
void present(Context *context, const Motion &motion, const Config &config);
void fini(Context *context);

} // namespace overlay
