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

static void rectangle(u32 *pixels, u32 stride, u32 width, u32 height,
                      s32 x, s32 y, s32 rectWidth, s32 rectHeight,
                      u32 color) {
    const s32 left = std::clamp<s32>(x, 0, static_cast<s32>(width));
    const s32 top = std::clamp<s32>(y, 0, static_cast<s32>(height));
    const s32 right = std::clamp<s32>(x + rectWidth, 0,
                                      static_cast<s32>(width));
    const s32 bottom = std::clamp<s32>(y + rectHeight, 0,
                                       static_cast<s32>(height));
    for (s32 row = top; row < bottom; ++row) {
        auto *line = reinterpret_cast<u32 *>(
            reinterpret_cast<u8 *>(pixels) + row * stride);
        std::fill(line + left, line + right, color);
    }
}

static void roundedRectangle(u32 *pixels, u32 stride, u32 width, u32 height,
                             s32 x, s32 y, s32 rectWidth, s32 rectHeight,
                             s32 radius, u32 color) {
    if (!pixels || rectWidth <= 0 || rectHeight <= 0) return;
    radius = std::clamp(radius, 0, std::min(rectWidth, rectHeight) / 2);
    if (radius == 0) {
        rectangle(pixels, stride, width, height, x, y,
                  rectWidth, rectHeight, color);
        return;
    }

    const float cornerCenterY = static_cast<float>(radius) - 0.5f;
    const float radiusSquared = static_cast<float>(radius * radius);
    for (s32 localY = 0; localY < rectHeight; ++localY) {
        const s32 screenY = y + localY;
        if (screenY < 0 || screenY >= static_cast<s32>(height)) continue;

        s32 inset = 0;
        if (localY < radius) {
            const float dy = cornerCenterY - static_cast<float>(localY);
            inset = radius - static_cast<s32>(
                std::sqrt(std::max(0.0f, radiusSquared - dy * dy)));
        } else if (localY >= rectHeight - radius) {
            const float dy = cornerCenterY -
                             static_cast<float>(rectHeight - 1 - localY);
            inset = radius - static_cast<s32>(
                std::sqrt(std::max(0.0f, radiusSquared - dy * dy)));
        }

        const s32 left = std::clamp(x + inset, 0, static_cast<s32>(width));
        const s32 right = std::clamp(x + rectWidth - inset, 0,
                                     static_cast<s32>(width));
        if (left >= right) continue;
        auto *line = reinterpret_cast<u32 *>(
            reinterpret_cast<u8 *>(pixels) + screenY * stride);
        std::fill(line + left, line + right, color);
    }
}

static u8 glyphRow(char character, unsigned row) {
    if (row >= 7) return 0;
    const u8 *glyph = nullptr;
    static constexpr u8 space[7] = {0, 0, 0, 0, 0, 0, 0};
    static constexpr u8 colon[7] = {0, 4, 4, 0, 4, 4, 0};
    static constexpr u8 c[7] = {14, 17, 16, 16, 16, 17, 14};
    static constexpr u8 e[7] = {31, 16, 16, 30, 16, 16, 31};
    static constexpr u8 i[7] = {14, 4, 4, 4, 4, 4, 14};
    static constexpr u8 l[7] = {16, 16, 16, 16, 16, 16, 31};
    static constexpr u8 m[7] = {17, 27, 21, 21, 17, 17, 17};
    static constexpr u8 n[7] = {17, 25, 21, 19, 17, 17, 17};
    static constexpr u8 o[7] = {14, 17, 17, 17, 17, 17, 14};
    static constexpr u8 r[7] = {30, 17, 17, 30, 20, 18, 17};
    static constexpr u8 s[7] = {15, 16, 16, 14, 1, 1, 30};
    static constexpr u8 t[7] = {31, 4, 4, 4, 4, 4, 4};
    switch (character) {
        case ' ': glyph = space; break;
        case ':': glyph = colon; break;
        case 'C': glyph = c; break;
        case 'E': glyph = e; break;
        case 'I': glyph = i; break;
        case 'L': glyph = l; break;
        case 'M': glyph = m; break;
        case 'N': glyph = n; break;
        case 'O': glyph = o; break;
        case 'R': glyph = r; break;
        case 'S': glyph = s; break;
        case 'T': glyph = t; break;
        default: glyph = space; break;
    }
    return glyph[row];
}

static void tinyText(u32 *pixels, u32 stride, u32 width, u32 height,
                     s32 x, s32 y, const char *text, s32 scale, u32 color) {
    if (!text || scale <= 0) return;
    for (s32 index = 0; text[index] != '\0'; ++index) {
        for (unsigned row = 0; row < 7; ++row) {
            const u8 bits = glyphRow(text[index], row);
            for (unsigned column = 0; column < 5; ++column) {
                if ((bits & (1U << (4U - column))) == 0) continue;
                rectangle(pixels, stride, width, height,
                          x + index * 6 * scale + column * scale,
                          y + static_cast<s32>(row) * scale,
                          scale, scale, color);
            }
        }
    }
}

static void sourceToast(u32 *pixels, u32 stride, u32 width, u32 height,
                        Motion::Source source, float seconds) {
    if (seconds <= 0.0f || source == Motion::Source::None) return;
    const char *label = source == Motion::Source::Console
                            ? "MOTION: CONSOLE"
                            : "MOTION: CONTROLLER";
    const float fade = std::clamp(seconds / 0.35f, 0.0f, 1.0f);
    const u8 coverage = static_cast<u8>(fade * 255.0f + 0.5f);
    const u32 shadow = swots::pixel::scalePremultiplied(
        swots::pixel::premultipliedRgba(0, 0, 0, 105), coverage);
    const u32 panel = swots::pixel::scalePremultiplied(
        swots::pixel::premultipliedRgba(35, 35, 39, 238), coverage);
    const u32 accent = swots::pixel::scalePremultiplied(
        source == Motion::Source::Console
            ? swots::pixel::premultipliedRgba(73, 176, 255, 255)
            : swots::pixel::premultipliedRgba(68, 207, 132, 255),
        coverage);
    const u32 text = swots::pixel::scalePremultiplied(
        swots::pixel::premultipliedRgba(248, 248, 250, 255), coverage);
    const u32 iconText = swots::pixel::scalePremultiplied(
        swots::pixel::premultipliedRgba(20, 24, 28, 255), coverage);
    constexpr s32 scale = 2;
    const s32 textWidth = static_cast<s32>(std::strlen(label)) * 6 * scale - scale;
    const s32 panelWidth = textWidth + 64;
    constexpr s32 panelHeight = 42;
    constexpr s32 left = 14;
    constexpr s32 top = 12;
    constexpr s32 panelRadius = 7;
    roundedRectangle(pixels, stride, width, height, left + 2, top + 3,
                     panelWidth, panelHeight, panelRadius, shadow);
    roundedRectangle(pixels, stride, width, height, left, top,
                     panelWidth, panelHeight, panelRadius, panel);

    // A compact app-style tile makes the in-layer toast read like a native
    // Switch notification without invoking or queueing system notifications.
    roundedRectangle(pixels, stride, width, height, left + 8, top + 8,
                     26, 26, 5, accent);
    tinyText(pixels, stride, width, height, left + 16, top + 14,
             "M", scale, iconText);
    tinyText(pixels, stride, width, height, left + 44, top + 14,
             label, scale, text);
}

void frame(u32 *pixels, u32 stride, u32 width, u32 height,
           const Motion &motion, const Config &config,
           Motion::Source toastSource, float toastSeconds) {
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
    sourceToast(pixels, stride, width, height, toastSource, toastSeconds);
}

} // namespace draw
