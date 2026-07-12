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
    bool initialized = false;
};

Result init(Context *context);
void render(Context *context, Motion &motion, const Config &config, float dt);
void fini(Context *context);

} // namespace overlay
