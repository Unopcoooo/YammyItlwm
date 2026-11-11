// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace ItlAwdl {
// A unicast master-indication request. The native manager supplies the TLVs;
// the radio wrapper supplies the 802.11 and AWDL action headers.
constexpr size_t kNativeUmiPayloadMax = 1200;
inline bool nativeUmiUnicast(const uint8_t *address) {
    if (!address || (address[0] & 1)) return false;
    uint8_t any = 0;
    for (unsigned i = 0; i < 6; ++i) any |= address[i];
    return any != 0;
}
inline bool nativeUmiPayload(const uint8_t *payload, size_t length) {
    return payload && length && length <= kNativeUmiPayloadMax;
}
inline bool buildNativeUmiFrame(const uint8_t peer[6], const uint8_t source[6],
                                const uint8_t *payload, size_t length, uint32_t gp2,
                                uint16_t sequence, uint8_t *out, size_t capacity,
                                size_t &used) {
    used = 0;
    if (!nativeUmiUnicast(peer) || !nativeUmiUnicast(source) ||
        !nativeUmiPayload(payload, length) || !out ||
        capacity < 40 + length) return false;
    static const uint8_t bssid[6] = {0, 0x25, 0, 0xff, 0x94, 0x73};
    memset(out, 0, 40);
    out[0] = 0xd0; // management action
    out[2] = 0x30; // 48 us duration, matching native unicast MI captures
    memcpy(out + 4, peer, 6);
    memcpy(out + 10, source, 6);
    memcpy(out + 16, bssid, 6);
    const uint16_t seq = uint16_t((sequence & 0x0fff) << 4);
    out[22] = uint8_t(seq);
    out[23] = uint8_t(seq >> 8);
    out[24] = 127; // vendor-specific
    out[26] = 0x17;
    out[27] = 0xf2;
    out[28] = 8;
    out[29] = 0x10;
    out[30] = 3; // master indication
    for (unsigned i = 0; i < 4; ++i) {
        out[32 + i] = uint8_t(gp2 >> (8 * i));
        out[36 + i] = uint8_t(gp2 >> (8 * i));
    }
    memcpy(out + 40, payload, length);
    used = 40 + length;
    return true;
}
}
