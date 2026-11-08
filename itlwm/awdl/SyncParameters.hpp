// SPDX-License-Identifier: GPL-2.0-or-later
// Wire decoder only: no scheduling, radio operations, or clock-domain conversion.
// Reference layout: seemoo-lab/owl src/frame.h awdl_sync_params_tlv.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "ActionFrame.hpp"
namespace ItlAwdl {
struct SyncParameters {
    uint8_t nextChannel, masterChannel, guardTime;
    uint16_t txDownCounter, awPeriod, afPeriod, flags;
    uint16_t awExtensionLength, awCommonLength, remainingAwLength;
    uint8_t minExtension, maxMulticastExtension, maxUnicastExtension, maxActionExtension;
    uint8_t masterAddress[6];
    uint8_t presenceMode;
    uint16_t nextAwSequence, apAlignment;
};
inline uint16_t syncRead16(const uint8_t *p) {
    return uint16_t(uint16_t(p[0]) | (uint16_t(p[1]) << 8));
}
// Input is TLV type 4's VALUE, excluding its 3-byte type/length envelope.
// A true result certifies only the fixed wire layout. Trailing channel sequence
// bytes are NOT decoded here and must not be assumed validated by this function.
inline bool decodeSyncParameters(const uint8_t *p, size_t n, SyncParameters &out) {
    out = {};
    if (!p || n < 33) return false;
    out.nextChannel = p[0]; out.txDownCounter = syncRead16(p + 1);
    out.masterChannel = p[3]; out.guardTime = p[4];
    out.awPeriod = syncRead16(p + 5); out.afPeriod = syncRead16(p + 7);
    out.flags = syncRead16(p + 9); out.awExtensionLength = syncRead16(p + 11);
    out.awCommonLength = syncRead16(p + 13); out.remainingAwLength = syncRead16(p + 15);
    out.minExtension = p[17]; out.maxMulticastExtension = p[18];
    out.maxUnicastExtension = p[19]; out.maxActionExtension = p[20];
    for (unsigned i = 0; i < 6; ++i) out.masterAddress[i] = p[21 + i];
    out.presenceMode = p[27]; // byte 28 is reserved
    out.nextAwSequence = syncRead16(p + 29); out.apAlignment = syncRead16(p + 31);
    return true;
}
enum class SyncExtraction { InvalidEnvelope, NoSync, MalformedSync, DuplicateSync, Found };
inline SyncExtraction extractSyncParameters(const uint8_t *frame, size_t length, SyncParameters &out) {
    out = {};
    Summary envelope;
    if (inspect(frame, length, envelope) != Result::Valid) return SyncExtraction::InvalidEnvelope;
    const size_t start = 24 + ((frame[1] & 0x80) ? 4 : 0) + 16;
    const uint8_t *p = frame + start;
    size_t remaining = length - start;
    bool found = false;
    SyncParameters candidate = {};
    // inspect() has already validated every TLV envelope in this complete frame.
    while (remaining) {
        const size_t valueLength = syncRead16(p + 1);
        if (p[0] == 4) {
            if (found) return SyncExtraction::DuplicateSync;
            if (!decodeSyncParameters(p + 3, valueLength, candidate)) return SyncExtraction::MalformedSync;
            found = true;
        }
        p += 3 + valueLength;
        remaining -= 3 + valueLength;
    }
    if (!found) return SyncExtraction::NoSync;
    out = candidate;
    return SyncExtraction::Found;
}
}
