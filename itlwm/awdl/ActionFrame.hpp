// SPDX-License-Identifier: GPL-2.0-or-later
// AWDL wire-envelope observer. No packet ownership, allocation, or radio control.
// Wire layout reference: seemoo-lab/owl src/frame.h (see tests/awdl/README.md).
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace ItlAwdl {
enum class Result : uint8_t { NotAwdl, Truncated, Unsupported, Malformed, Valid };
struct Summary {
    uint8_t version;
    uint8_t subtype;
    uint16_t tlvs;
    bool sync;
    bool election;
    bool channelSequence;
};
// Input is one complete, contiguous 802.11 MPDU without radiotap or trailing FCS.
// Caller must not interpret Valid as semantic validity of individual TLV values.
inline Result inspect(const uint8_t *p, size_t n, Summary &out) {
    out = {};
    if (!p || n < 2) return Result::Truncated;
    if (p[0] != 0xd0) return Result::NotAwdl; // v0 management Action
    // Do not parse protected, fragmented, or DS-addressed management frames.
    if (p[1] & 0x47) return Result::Unsupported;
    const size_t hdr = 24 + ((p[1] & 0x80) ? 4 : 0); // optional HT control
    if (n < hdr) return Result::Truncated;
    if (p[22] & 0x0f) return Result::Unsupported;
    p += hdr; n -= hdr;
    if (n < 5) return Result::Truncated;
    if (p[0] != 127 || p[1] != 0 || p[2] != 0x17 || p[3] != 0xf2 || p[4] != 8)
        return Result::NotAwdl;
    if (n < 16) return Result::Truncated;
    Summary candidate = {};
    candidate.version = p[5]; candidate.subtype = p[6];
    if (p[5] != 0x10 || (p[6] != 0 && p[6] != 3)) return Result::Unsupported;
    p += 16; n -= 16;
    while (n) {
        if (n < 3) return Result::Malformed;
        const size_t length = size_t(p[1]) | (size_t(p[2]) << 8);
        if (length > n - 3 || candidate.tlvs == UINT16_MAX) return Result::Malformed;
        candidate.sync |= p[0] == 4;
        candidate.election |= p[0] == 5 || p[0] == 24;
        candidate.channelSequence |= p[0] == 18;
        ++candidate.tlvs;
        p += 3 + length; n -= 3 + length;
    }
    out = candidate;
    return Result::Valid;
}
} // namespace ItlAwdl
