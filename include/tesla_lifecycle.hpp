// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>

namespace tesla_lifecycle {

inline constexpr std::uint32_t Magic = 0x53574F54U; // "SWOT"
inline constexpr std::uint32_t AckMagic = 0x5357414BU; // "SWAK"
inline constexpr std::uint16_t Version = 1;

enum class State : std::uint8_t {
    Visible = 1,
    Hidden = 2,
    Parent = 3,
};

struct Record {
    std::uint32_t magic = Magic;
    std::uint16_t version = Version;
    State state = State::Visible;
    std::uint8_t reserved = 0;
    std::uint64_t session = 0;
    std::uint64_t generation = 0;
};

static_assert(sizeof(Record) == 24);

struct Ack {
    std::uint32_t magic = AckMagic;
    std::uint16_t version = Version;
    std::uint16_t reserved = 0;
    std::uint64_t session = 0;
    std::uint64_t generation = 0;
};

static_assert(sizeof(Ack) == 24);

inline bool valid(const Record &record) {
    return record.magic == Magic && record.version == Version &&
           record.reserved == 0 && record.session != 0 &&
           record.generation != 0 &&
           (record.state == State::Visible || record.state == State::Hidden ||
            record.state == State::Parent);
}

inline bool valid(const Ack &ack) {
    return ack.magic == AckMagic && ack.version == Version &&
           ack.reserved == 0 && ack.session != 0 && ack.generation != 0;
}

} // namespace tesla_lifecycle
