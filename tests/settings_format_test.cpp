// SPDX-License-Identifier: GPL-2.0-or-later

#include "settings_format.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

using swots::settings::Values;

namespace {

void fail(const char *expression, int line) {
    std::cerr << "settings_format_test:" << line << ": check failed: "
              << expression << '\n';
    std::exit(1);
}

#define CHECK(expression) ((expression) ? void() : fail(#expression, __LINE__))

bool same(const Values &a, const Values &b) {
    return a.opacity == b.opacity && a.dotRadius == b.dotRadius &&
           a.sensitivity == b.sensitivity && a.smoothing == b.smoothing;
}

bool parseText(const std::string &text, const Values &base, Values *out) {
    return swots::settings::parse(text.data(), text.size(), base, out);
}

}  // namespace

int main() {
    const Values defaults{};
    Values parsed{};

    CHECK(swots::settings::opacityToPercent(0) == 0);
    CHECK(swots::settings::opacityToPercent(255) == 100);
    CHECK(swots::settings::percentToOpacity(0) == 0);
    CHECK(swots::settings::percentToOpacity(100) == 255);
    CHECK(swots::settings::percentToOpacity(200) == 255);
    for (std::uint8_t radius = swots::settings::kMinDotRadius;
         radius <= swots::settings::kMaxDotRadius; ++radius) {
        CHECK(swots::settings::stepToDotRadius(
                  swots::settings::dotRadiusToStep(radius)) == radius);
    }
    CHECK(swots::settings::stepToDotRadius(99) ==
          swots::settings::kMaxDotRadius);

    CHECK(parseText("version=1\nopacity=0\ndot_radius=12\n"
                    "sensitivity=100\nsmoothing=0\n",
                    defaults, &parsed));
    CHECK(parsed.opacity == 0);
    CHECK(parsed.dotRadius == 12);
    CHECK(parsed.sensitivity == 100);
    CHECK(parsed.smoothing == 0);

    Values base{};
    base.opacity = 17;
    base.dotRadius = 8;
    base.sensitivity = 33;
    base.smoothing = 44;
    CHECK(parseText("version=1\nopacity=256\ndot_radius=1\n"
                    "sensitivity=-1\nsmoothing= 5\nunknown=9\n",
                    base, &parsed));
    CHECK(same(parsed, base));

    CHECK(parseText("version=1\r\nopacity=1\r\nopacity=255\r\n"
                    "dot_radius=2\r\nsensitivity=01\r\nsmoothing=100\r\n",
                    defaults, &parsed));
    CHECK(parsed.opacity == 255);
    CHECK(parsed.dotRadius == 2);
    CHECK(parsed.sensitivity == 1);
    CHECK(parsed.smoothing == 100);

    CHECK(parseText("opacity=5\nversion=1", defaults, &parsed));
    CHECK(parsed.opacity == 5);

    parsed = base;
    CHECK(!parseText("opacity=5\n", defaults, &parsed));
    CHECK(same(parsed, defaults));
    CHECK(!parseText("version=2\nopacity=5\n", base, &parsed));
    CHECK(same(parsed, base));
    CHECK(!parseText("version=1\nversion=x\nopacity=5\n", base, &parsed));
    CHECK(same(parsed, base));

    CHECK(parseText("version=1\nopacity=9\nopacity=x\n", base, &parsed));
    CHECK(parsed.opacity == base.opacity);
    CHECK(parseText("version=1\nopacity=x\nopacity=9\n", base, &parsed));
    CHECK(parsed.opacity == 9);

    std::string tooLong(swots::settings::kMaxTextSize + 1, 'x');
    CHECK(!parseText(tooLong, base, &parsed));
    std::string nonAscii = "version=1\nunknown=";
    nonAscii.push_back(static_cast<char>(0x80));
    CHECK(!parseText(nonAscii, base, &parsed));
    const std::string embeddedNul("version=1\0opacity=4", 19);
    CHECK(!parseText(embeddedNul, base, &parsed));

    char text[swots::settings::kMaxTextSize];
    const std::size_t length =
        swots::settings::serialize(base, text, sizeof(text));
    CHECK(length != 0 && length <= swots::settings::kMaxTextSize);
    CHECK(parseText(std::string(text, length), defaults, &parsed));
    CHECK(same(parsed, base));
    CHECK(std::string(text, length) ==
          "version=1\nopacity=17\ndot_radius=8\nsensitivity=33\nsmoothing=44\n");
    CHECK(swots::settings::serialize(base, text, 8) == 0);

    Values invalid = defaults;
    invalid.dotRadius = 13;
    CHECK(swots::settings::serialize(invalid, text, sizeof(text)) == 0);
    invalid = defaults;
    invalid.sensitivity = 101;
    CHECK(swots::settings::serialize(invalid, text, sizeof(text)) == 0);

    std::cout << "settings format tests passed\n";
    return 0;
}
