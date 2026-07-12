// SPDX-License-Identifier: GPL-2.0-or-later

#include "draw.hpp"
#include "pixel_math.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace draw {

static void circle(u32 *pixels, u32 stride, u32 width, u32 height,
                   float centerX, float centerY, float radius, u32 color) {
    const s32 minY = std::max<s32>(
        0, static_cast<s32>(std::floor(centerY - radius - 0.5f)));
    const s32 maxY = std::min<s32>(
        static_cast<s32>(height) - 1,
        static_cast<s32>(std::ceil(centerY + radius + 0.5f)));
    const s32 minXBound = std::max<s32>(
        0, static_cast<s32>(std::floor(centerX - radius - 0.5f)));
    const s32 maxXBound = std::min<s32>(
        static_cast<s32>(width) - 1,
        static_cast<s32>(std::ceil(centerX + radius + 0.5f)));

    for (s32 y = minY; y <= maxY; ++y) {
        auto *row = reinterpret_cast<u32 *>(reinterpret_cast<u8 *>(pixels) + y * stride);
        for (s32 x = minXBound; x <= maxXBound; ++x) {
            const float dx = (static_cast<float>(x) + 0.5f) - centerX;
            const float dy = (static_cast<float>(y) + 0.5f) - centerY;
            const float distance = std::sqrt(dx * dx + dy * dy);
            const float coverage = std::clamp(radius + 0.5f - distance, 0.0f, 1.0f);
            if (coverage <= 0.0f) continue;
            row[x] = swots::pixel::scalePremultiplied(
                color, static_cast<u8>(coverage * 255.0f + 0.5f));
        }
    }
}

static float wrap(float value, float span) {
    value = std::fmod(value, span);
    if (value < 0.0f) value += span;
    return value;
}

void frame(u32 *pixels, u32 stride, u32 width, u32 height,
           const Motion &motion, const Config &config) {
    for (u32 y = 0; y < height; ++y) {
        std::memset(reinterpret_cast<u8 *>(pixels) + y * stride, 0, width * sizeof(u32));
    }
    if (!config.enabled) return;

    const float centerX = static_cast<float>(width) * 0.5f;
    const float centerY = static_cast<float>(height) * 0.5f;
    const float halfDiagonal = std::sqrt(centerX * centerX + centerY * centerY);
    const float spacing = static_cast<float>(std::max<u8>(24, config.dotSpacing));
    const float cosRoll = std::cos(motion.roll());
    const float sinRoll = std::sin(motion.roll());
    const u32 color = swots::pixel::premultipliedRgba(
        config.dotColor.r, config.dotColor.g, config.dotColor.b, config.opacity);

    // Overscan by two cells so rotation never reveals an empty corner.
    const s32 columns = static_cast<s32>(width / spacing) + 5;
    const s32 rows = static_cast<s32>(height / spacing) + 5;
    for (s32 row = -2; row < rows; ++row) {
        for (s32 column = -2; column < columns; ++column) {
            float localX = wrap(column * spacing + motion.offsetX(), columns * spacing) - centerX;
            float localY = wrap(row * spacing + motion.offsetY(), rows * spacing) - centerY;
            const float x = centerX + localX * cosRoll + localY * sinRoll;
            const float y = centerY - localX * sinRoll + localY * cosRoll;

            // Keep the gameplay focal area clean and grow cues toward edges.
            const float distance = std::sqrt((x - centerX) * (x - centerX) +
                                             (y - centerY) * (y - centerY)) / halfDiagonal;
            const float t = std::clamp((distance - 0.32f) / (0.82f - 0.32f), 0.0f, 1.0f);
            const float focus = t * t * (3.0f - 2.0f * t);
            const float radius = static_cast<float>(config.dotRadius) * focus;
            if (radius > 0.05f) {
                circle(pixels, stride, width, height, x, y, radius, color);
            }
        }
    }
}

} // namespace draw
