// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>

namespace swots::pixel {

// Switch VI's software framebuffer is composited as premultiplied alpha.
// Supplying full-bright RGB with a low alpha saturates light-colored dots,
// making every opacity setting appear solid.
inline constexpr std::uint32_t premultipliedRgba(std::uint8_t red,
                                                  std::uint8_t green,
                                                  std::uint8_t blue,
                                                  std::uint8_t alpha) noexcept {
    const auto scale = [alpha](std::uint8_t channel) constexpr {
        return static_cast<std::uint8_t>(
            (static_cast<unsigned>(channel) * alpha + 127U) / 255U);
    };
    return static_cast<std::uint32_t>(scale(red)) |
           (static_cast<std::uint32_t>(scale(green)) << 8) |
           (static_cast<std::uint32_t>(scale(blue)) << 16) |
           (static_cast<std::uint32_t>(alpha) << 24);
}

inline constexpr std::uint32_t scalePremultiplied(std::uint32_t color,
                                                   std::uint8_t coverage) noexcept {
    std::uint32_t result = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) {
        const unsigned channel = (color >> shift) & 0xffU;
        const unsigned scaled = (channel * coverage + 127U) / 255U;
        result |= static_cast<std::uint32_t>(scaled) << shift;
    }
    return result;
}

} // namespace swots::pixel
