// SPDX-License-Identifier: GPL-2.0-or-later
// Independent wire decoder; layout references: OWL frame.h and channel.h.
#pragma once
#include "SyncParameters.hpp"
namespace ItlAwdl {
constexpr unsigned kChannelSequenceSlots = 16;
struct ChannelEntry { uint8_t channel, auxiliary; };
struct ChannelSequence {
    uint16_t count, steps, fill;
    uint8_t encoding, duplicates;
    ChannelEntry entries[kChannelSequenceSlots];
};
enum class ChannelDecode { Valid, Truncated, Unsupported };
// Decode a sequence prefix, not its enclosing TLV. consumed excludes padding.
// Cap storage at 16 entries; retain metadata without pretending it is supported
// by a radio scheduler. auxiliary means legacy flags or operating class.
inline ChannelDecode decodeChannelSequence(const uint8_t *p, size_t n,
                                           ChannelSequence &out, size_t &consumed) {
    out = {}; consumed = 0;
    if (!p || n < 6) return ChannelDecode::Truncated;
    const unsigned count = unsigned(p[0]) + 1;
    const unsigned width = p[1] == 0 ? 1 : (p[1] == 1 || p[1] == 3 ? 2 : 0);
    if (!width || count > kChannelSequenceSlots) return ChannelDecode::Unsupported;
    const size_t required = 6 + count * width;
    if (n < required) return ChannelDecode::Truncated;
    ChannelSequence result = {};
    result.count = uint16_t(count); result.steps = uint16_t(unsigned(p[3]) + 1);
    result.fill = syncRead16(p + 4);
    result.encoding = p[1]; result.duplicates = p[2];
    for (unsigned i = 0; i < count; ++i) {
        const uint8_t *entry = p + 6 + i * width;
        result.entries[i].channel = entry[p[1] == 1 ? 1 : 0];
        result.entries[i].auxiliary = width == 1 ? 0 : entry[p[1] == 1 ? 0 : 1];
    }
    out = result; consumed = required;
    return ChannelDecode::Valid;
}
}
