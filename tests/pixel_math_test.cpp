// SPDX-License-Identifier: GPL-2.0-or-later

#include "pixel_math.hpp"

#include <cstdint>
#include <cstdio>

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

std::uint8_t channel(std::uint32_t color, unsigned shift) {
    return static_cast<std::uint8_t>((color >> shift) & 0xffU);
}

} // namespace

int main() {
    using swots::pixel::premultipliedRgba;

    CHECK(premultipliedRgba(244, 247, 255, 255) == 0xfffff7f4U);
    CHECK(premultipliedRgba(244, 247, 255, 0) == 0U);

    const std::uint32_t translucent = premultipliedRgba(244, 247, 255, 71);
    CHECK(channel(translucent, 0) == 68);
    CHECK(channel(translucent, 8) == 69);
    CHECK(channel(translucent, 16) == 71);
    CHECK(channel(translucent, 24) == 71);
    CHECK(swots::pixel::scalePremultiplied(translucent, 0) == 0U);
    CHECK(swots::pixel::scalePremultiplied(translucent, 255) == translucent);
    const std::uint32_t halfCovered =
        swots::pixel::scalePremultiplied(translucent, 128);
    CHECK(channel(halfCovered, 0) == 34);
    CHECK(channel(halfCovered, 24) == 36);

    if (failures != 0) return 1;
    std::puts("pixel math tests passed");
    return 0;
}
