// SPDX-License-Identifier: GPL-2.0-or-later
// Offline arithmetic model only. No peer trust, clock calibration or radio I/O.
#pragma once
#include "SyncParameters.hpp"
namespace ItlAwdl {
struct WindowPrediction {
    uint32_t nextBoundaryGp2;
    uint32_t waitMicroseconds;
    uint16_t nextBoundarySequence;
};
// Model assumptions (must be validated on real frames before scheduling):
// nextAwSequence identifies the current AW; down-counter reaches the next EAW
// boundary; receiveGp2 anchors reception in the same microsecond clock as nowGp2.
// A finite caller-supplied freshness limit rejects stale/ambiguous observations.
// GP2 subtraction is valid only for a forward interval shorter than 2^31 us;
// caller must discard observations on firmware reset and across full wraps.
// AF values are current at target TX, before the measured sender queue delay.
// Keep freshness anchored to physical RX; correct only the predicted boundary.
inline bool predictWindowWithTxDelay(const SyncParameters &sync,uint32_t receiveGp2,
                          uint32_t nowGp2,uint32_t maxAgeUs,uint32_t txDelayUs,WindowPrediction &out) {
    out = {};
    if (!sync.awPeriod || !sync.presenceMode || !maxAgeUs || maxAgeUs >= 0x80000000u)
        return false;
    const uint64_t periodTu = uint64_t(sync.awPeriod) * sync.presenceMode;
    if (sync.txDownCounter > periodTu) return false;
    const uint32_t age = nowGp2 - receiveGp2;
    if (age > maxAgeUs) return false;
    const uint64_t periodUs = periodTu * 1024;
    if (periodUs >= 0x80000000ULL || txDelayUs > periodUs) return false;
    int64_t offset = int64_t(sync.txDownCounter) * 1024 - txDelayUs;
    uint64_t periods = 0;
    // Boundary at 'now' is treated as already reached; return a strictly future one.
    if (offset <= int64_t(age)) {
        periods = uint64_t(int64_t(age) - offset) / periodUs + 1;
        offset += periods * periodUs;
    }
    const uint64_t base = sync.nextAwSequence - sync.nextAwSequence % sync.presenceMode;
    out.nextBoundaryGp2 = receiveGp2 + uint32_t(offset);
    out.waitMicroseconds = uint32_t(offset - age);
    out.nextBoundarySequence = uint16_t(base + (periods + 1) * sync.presenceMode);
    return true;
}
// Preserve zero-delay arithmetic for existing consumers and regression fixtures.
inline bool predictWindow(const SyncParameters &sync,uint32_t received,uint32_t now,
                          uint32_t maxAge,WindowPrediction &out) {
    return predictWindowWithTxDelay(sync,received,now,maxAge,0,out);
}
}
