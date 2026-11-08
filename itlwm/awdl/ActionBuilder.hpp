// SPDX-License-Identifier: GPL-2.0-or-later
// Independent bounded encoder of a CONTROL-ONLY experimental AWDL action frame.
// Wire-layout facts: OWL frame.h. Not a complete interoperable advertisement.
// No names, service records, datapath state, radiotap or FCS. No radio calls.
#pragma once
#include "ChannelSchedule.hpp"
#include "ElectionParameters.hpp"
namespace ItlAwdl {
constexpr size_t kControlActionLength = 227;
struct ActionParameters {
    uint8_t source[6];
    uint8_t subtype; // 0 PSF or 3 MIF; same limited TLV set in this experiment
    uint16_t sequence; // 12-bit 802.11 sequence, not the AW counter
    uint32_t phyTransmitTime, targetTransmitTime;
    SyncParameters sync;
    ChannelSequence channels;
    ElectionParameters election;
};
inline void actionWrite16(uint8_t *p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
inline void actionWrite32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i));
}
inline uint32_t actionRead32(const uint8_t *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
        (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline bool actionUnicastAddress(const uint8_t p[6]) {
    uint8_t any = 0;
    for (unsigned i = 0; i < 6; ++i) any |= p[i];
    return any && !(p[0] & 1);
}
inline void actionEncodeSequence(uint8_t *p, const ChannelSequence &s) {
    p[0] = 15; p[1] = 3; p[2] = 0; p[3] = uint8_t(s.steps - 1);
    actionWrite16(p + 4, s.fill);
    for (unsigned i = 0; i < 16; ++i) {
        p[6 + 2 * i] = s.entries[i].channel;
        p[7 + 2 * i] = s.entries[i].auxiliary;
    }
}
// Preflight all requirements before touching output. Caller must generate
// truthful sync/election/channel parameters and refresh TX times before using
// this frame; successful encoding does not prove scheduling or peer acceptance.
inline bool buildControlAction(const ActionParameters &a, uint8_t *out, size_t capacity, size_t &used) {
    used = 0;
    if (!out || capacity < kControlActionLength || !actionUnicastAddress(a.source) ||
        !actionUnicastAddress(a.sync.masterAddress) || !actionUnicastAddress(a.election.masterAddress) ||
        !actionUnicastAddress(a.election.syncAddress) || !a.election.v2 ||
        (a.subtype != 0 && a.subtype != 3) || a.sequence > 4095 || a.election.height > 255 ||
        !scheduleConsistentWithSync(a.sync, a.channels) || a.channels.encoding != 3 ||
        !a.sync.awPeriod || a.sync.txDownCounter > uint32_t(a.sync.awPeriod) * a.sync.presenceMode)
        return false;
    for (unsigned i = 0; i < 6; ++i)
        if (a.sync.masterAddress[i] != a.election.masterAddress[i]) return false;
    for (size_t i = 0; i < kControlActionLength; ++i) out[i] = 0;
    out[0] = 0xd0;
    const uint8_t bssid[6] = {0, 0x25, 0, 0xff, 0x94, 0x73};
    for (unsigned i = 0; i < 6; ++i) { out[4+i] = 255; out[10+i] = a.source[i]; out[16+i] = bssid[i]; }
    actionWrite16(out + 22, uint16_t(a.sequence << 4));
    out[24] = 127; out[26] = 0x17; out[27] = 0xf2; out[28] = 8; out[29] = 0x10; out[30] = a.subtype;
    actionWrite32(out + 32, a.phyTransmitTime); actionWrite32(out + 36, a.targetTransmitTime);
    // Sync TLV: fixed 33 bytes + opclass sequence 38 bytes + 2 padding bytes.
    out[40] = 4; actionWrite16(out + 41, 73);
    uint8_t *s = out + 43;
    s[0] = a.sync.nextChannel; actionWrite16(s + 1, a.sync.txDownCounter);
    s[3] = a.sync.masterChannel; s[4] = a.sync.guardTime;
    actionWrite16(s + 5, a.sync.awPeriod); actionWrite16(s + 7, a.sync.afPeriod);
    actionWrite16(s + 9, a.sync.flags); actionWrite16(s + 11, a.sync.awExtensionLength);
    actionWrite16(s + 13, a.sync.awCommonLength); actionWrite16(s + 15, a.sync.remainingAwLength);
    s[17] = a.sync.minExtension; s[18] = a.sync.maxMulticastExtension;
    s[19] = a.sync.maxUnicastExtension; s[20] = a.sync.maxActionExtension;
    for (unsigned i = 0; i < 6; ++i) s[21+i] = a.sync.masterAddress[i];
    s[27] = a.sync.presenceMode;
    actionWrite16(s + 29, a.sync.nextAwSequence); actionWrite16(s + 31, a.sync.apAlignment);
    actionEncodeSequence(s + 33, a.channels);
    // Standalone channel TLV: same sequence + 3 padding bytes.
    out[116] = 18; actionWrite16(out + 117, 41); actionEncodeSequence(out + 119, a.channels);
    // Election v1, including two trailing pad bytes.
    out[160] = 5; actionWrite16(out + 161, 21);
    uint8_t *one = out + 163; one[3] = uint8_t(a.election.height);
    for (unsigned i = 0; i < 6; ++i) one[5+i] = a.election.masterAddress[i];
    actionWrite32(one + 11, a.election.masterMetric); actionWrite32(one + 15, a.election.selfMetric);
    // Election v2.
    out[184] = 24; actionWrite16(out + 185, 40);
    uint8_t *two = out + 187;
    for (unsigned i = 0; i < 6; ++i) { two[i] = a.election.masterAddress[i]; two[6+i] = a.election.syncAddress[i]; }
    actionWrite32(two + 12, a.election.masterCounter); actionWrite32(two + 16, a.election.height);
    actionWrite32(two + 20, a.election.masterMetric); actionWrite32(two + 24, a.election.selfMetric);
    actionWrite32(two + 36, a.election.selfCounter);
    used = kControlActionLength;
    return true;
}
}
