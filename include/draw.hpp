// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <switch.h>
#include "config.hpp"
#include "motion.hpp"

namespace draw {

void frame(u32 *pixels, u32 stride, u32 width, u32 height,
           const Motion &motion, const Config &config);

} // namespace draw
