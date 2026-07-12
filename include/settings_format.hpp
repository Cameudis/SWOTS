// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace swots::settings {

inline constexpr std::size_t kMaxTextSize = 256;
inline constexpr unsigned kFormatVersion = 1;
inline constexpr std::uint8_t kMinOpacity = 0;
inline constexpr std::uint8_t kMaxOpacity = 255;
inline constexpr std::uint8_t kMinDotRadius = 2;
inline constexpr std::uint8_t kMaxDotRadius = 12;
inline constexpr std::uint8_t kMinPercent = 0;
inline constexpr std::uint8_t kMaxPercent = 100;

struct Values {
    std::uint8_t opacity = 210;
    std::uint8_t dotRadius = 5;
    std::uint8_t sensitivity = 55;
    std::uint8_t smoothing = 70;
};

inline constexpr std::uint8_t opacityToPercent(std::uint8_t opacity) noexcept {
    return static_cast<std::uint8_t>((static_cast<unsigned>(opacity) * 100U + 127U) /
                                     255U);
}

inline constexpr std::uint8_t percentToOpacity(std::uint8_t percent) noexcept {
    const unsigned bounded = percent > kMaxPercent ? kMaxPercent : percent;
    return static_cast<std::uint8_t>((bounded * 255U + 50U) / 100U);
}

inline constexpr std::uint8_t dotRadiusToStep(std::uint8_t radius) noexcept {
    if (radius <= kMinDotRadius) return 0;
    if (radius >= kMaxDotRadius) return kMaxDotRadius - kMinDotRadius;
    return radius - kMinDotRadius;
}

inline constexpr std::uint8_t stepToDotRadius(std::uint8_t step) noexcept {
    const unsigned maxStep = kMaxDotRadius - kMinDotRadius;
    const unsigned bounded = step > maxStep ? maxStep : step;
    return static_cast<std::uint8_t>(kMinDotRadius + bounded);
}

namespace detail {

inline bool parseDecimal(const char *text, std::size_t length,
                         unsigned maximum, unsigned *value) noexcept {
    if (text == nullptr || value == nullptr || length == 0) return false;

    unsigned parsed = 0;
    for (std::size_t i = 0; i < length; ++i) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if (ch < '0' || ch > '9') return false;
        const unsigned digit = ch - '0';
        if (parsed > (maximum - digit) / 10) return false;
        parsed = parsed * 10 + digit;
    }

    *value = parsed;
    return true;
}

inline bool keyEquals(const char *key, std::size_t length,
                      const char *expected) noexcept {
    const std::size_t expectedLength = std::strlen(expected);
    return length == expectedLength &&
           std::memcmp(key, expected, expectedLength) == 0;
}

inline bool appendText(char *out, std::size_t capacity, std::size_t *position,
                       const char *text) noexcept {
    const std::size_t length = std::strlen(text);
    if (*position > capacity || length > capacity - *position) return false;
    std::memcpy(out + *position, text, length);
    *position += length;
    return true;
}

inline bool appendDecimal(char *out, std::size_t capacity,
                          std::size_t *position, unsigned value) noexcept {
    char reversed[10];
    std::size_t length = 0;
    do {
        reversed[length++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0);

    if (*position > capacity || length > capacity - *position) return false;
    while (length != 0) out[(*position)++] = reversed[--length];
    return true;
}

inline bool appendSetting(char *out, std::size_t capacity,
                          std::size_t *position, const char *key,
                          unsigned value) noexcept {
    return appendText(out, capacity, position, key) &&
           appendText(out, capacity, position, "=") &&
           appendDecimal(out, capacity, position, value) &&
           appendText(out, capacity, position, "\n");
}

}  // namespace detail

// Parses a complete settings document. Missing and invalid setting values retain
// their value from base. A missing, invalid, or unsupported version rejects the
// complete document and leaves out equal to base. For duplicate keys, the final
// occurrence wins (an invalid final occurrence therefore restores the base value).
inline bool parse(const char *text, std::size_t length, const Values &base,
                  Values *out) noexcept {
    if (out == nullptr) return false;
    *out = base;
    if (text == nullptr || length == 0 || length > kMaxTextSize) return false;

    for (std::size_t i = 0; i < length; ++i) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if (ch == 0 || ch > 0x7f) return false;
    }

    Values parsed = base;
    bool versionSeen = false;
    bool versionValid = false;
    std::size_t lineStart = 0;
    while (lineStart < length) {
        std::size_t lineEnd = lineStart;
        while (lineEnd < length && text[lineEnd] != '\n') ++lineEnd;
        std::size_t contentEnd = lineEnd;
        if (contentEnd > lineStart && text[contentEnd - 1] == '\r') --contentEnd;

        std::size_t equals = lineStart;
        while (equals < contentEnd && text[equals] != '=') ++equals;
        if (equals < contentEnd) {
            const char *key = text + lineStart;
            const std::size_t keyLength = equals - lineStart;
            const char *valueText = text + equals + 1;
            const std::size_t valueLength = contentEnd - equals - 1;
            unsigned value = 0;

            if (detail::keyEquals(key, keyLength, "version")) {
                versionSeen = true;
                versionValid = detail::parseDecimal(
                                   valueText, valueLength, kFormatVersion, &value) &&
                               value == kFormatVersion;
            } else if (detail::keyEquals(key, keyLength, "opacity")) {
                parsed.opacity = base.opacity;
                if (detail::parseDecimal(valueText, valueLength, kMaxOpacity, &value))
                    parsed.opacity = static_cast<std::uint8_t>(value);
            } else if (detail::keyEquals(key, keyLength, "dot_radius")) {
                parsed.dotRadius = base.dotRadius;
                if (detail::parseDecimal(valueText, valueLength, kMaxDotRadius, &value) &&
                    value >= kMinDotRadius)
                    parsed.dotRadius = static_cast<std::uint8_t>(value);
            } else if (detail::keyEquals(key, keyLength, "sensitivity")) {
                parsed.sensitivity = base.sensitivity;
                if (detail::parseDecimal(valueText, valueLength, kMaxPercent, &value))
                    parsed.sensitivity = static_cast<std::uint8_t>(value);
            } else if (detail::keyEquals(key, keyLength, "smoothing")) {
                parsed.smoothing = base.smoothing;
                if (detail::parseDecimal(valueText, valueLength, kMaxPercent, &value))
                    parsed.smoothing = static_cast<std::uint8_t>(value);
            }
        }

        lineStart = lineEnd + (lineEnd < length ? 1 : 0);
    }

    if (!versionSeen || !versionValid) return false;
    *out = parsed;
    return true;
}

// Writes the canonical version-1 representation without a terminating NUL.
// Returns its byte length, or zero when the values/capacity are invalid.
inline std::size_t serialize(const Values &values, char *out,
                             std::size_t capacity) noexcept {
    if (out == nullptr || values.opacity < kMinOpacity || values.opacity > kMaxOpacity ||
        values.dotRadius < kMinDotRadius || values.dotRadius > kMaxDotRadius ||
        values.sensitivity < kMinPercent || values.sensitivity > kMaxPercent ||
        values.smoothing < kMinPercent || values.smoothing > kMaxPercent)
        return 0;

    if (capacity > kMaxTextSize) capacity = kMaxTextSize;
    std::size_t position = 0;
    if (!detail::appendSetting(out, capacity, &position, "version", kFormatVersion) ||
        !detail::appendSetting(out, capacity, &position, "opacity", values.opacity) ||
        !detail::appendSetting(out, capacity, &position, "dot_radius", values.dotRadius) ||
        !detail::appendSetting(out, capacity, &position, "sensitivity", values.sensitivity) ||
        !detail::appendSetting(out, capacity, &position, "smoothing", values.smoothing))
        return 0;
    return position;
}

}  // namespace swots::settings
