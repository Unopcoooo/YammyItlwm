// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "ChannelSchedule.hpp"
namespace ItlAwdl {
// Consecutive advertisements from ONE peer must predict compatible phase and
// AW counters. This tests internal consistency, not RF accuracy or peer trust.
inline int64_t signedGp2Delta(uint32_t a, uint32_t b) {
    const uint32_t d = a-b;
    return d < 0x80000000u ? int64_t(d) : int64_t(d)-0x100000000LL;
}
enum class TimingReason : uint32_t {
    FirstObservation=1, DuplicateTimestamp, PeriodChanged, ScheduleChanged,
    ObservationStale, PredictionInvalid, CounterMismatch, ResidualTooLarge, Consistent
};
struct TimingAudit {
    SyncParameters previous = {};
    ChannelSequence channels = {};
    uint32_t receivedGp2 = 0, streak = 0, txDelayUs=0;
    int32_t residualUs = 0;
    bool compared = false, consistent = false;
    TimingReason reason=TimingReason::FirstObservation;
    uint32_t gapUs=0, priorRxGp2=0, priorAwSequence=0, priorDownCounter=0,
        priorAwPeriod=0, priorPresenceMode=0, oldBoundaryGp2=0, newBoundaryGp2=0,
        sequenceDeltaRaw=0, changedSlot=UINT32_MAX;
    void observe(const SyncParameters &s, const ChannelSequence &c, uint32_t rx,uint32_t delay=0) {
        compared = consistent = false; residualUs = 0;
        reason=TimingReason::FirstObservation;gapUs=streak ? rx-receivedGp2 : 0;
        priorRxGp2=receivedGp2;priorAwSequence=previous.nextAwSequence;priorDownCounter=previous.txDownCounter;
        priorAwPeriod=previous.awPeriod;priorPresenceMode=previous.presenceMode;
        oldBoundaryGp2=newBoundaryGp2=sequenceDeltaRaw=0;changedSlot=UINT32_MAX;
        if (streak && rx == receivedGp2) {reason=TimingReason::DuplicateTimestamp;return;} // duplicate timestamp adds no confidence
        bool same = streak && c.count <= kChannelSequenceSlots && c.fill == channels.fill &&
            c.duplicates == channels.duplicates && s.awPeriod == previous.awPeriod && s.presenceMode == previous.presenceMode &&
            c.count == channels.count && c.steps == channels.steps && c.encoding == channels.encoding;
        for (unsigned i=0; same && i<c.count; ++i)
            same = c.entries[i].channel == channels.entries[i].channel &&
                   c.entries[i].auxiliary == channels.entries[i].auxiliary;
        if (streak) {
            if (s.awPeriod != previous.awPeriod || s.presenceMode != previous.presenceMode) reason=TimingReason::PeriodChanged;
            else if (!same) reason=TimingReason::ScheduleChanged;
            else if (gapUs>500000) reason=TimingReason::ObservationStale;
            else reason=TimingReason::PredictionInvalid;
        }
        for (unsigned i=0; i<c.count && i<kChannelSequenceSlots && i<channels.count; ++i)
            if (c.entries[i].channel!=channels.entries[i].channel || c.entries[i].auxiliary!=channels.entries[i].auxiliary) {changedSlot=i;break;}
        WindowPrediction oldWindow = {}, newWindow = {};
        if (same && predictWindowWithTxDelay(previous, receivedGp2, rx, 500000,txDelayUs,oldWindow) &&
            predictWindowWithTxDelay(s,rx,rx,500000,delay,newWindow)) {
            compared = true;
            oldBoundaryGp2=oldWindow.nextBoundaryGp2;newBoundaryGp2=newWindow.nextBoundaryGp2;
            const uint16_t raw = uint16_t(newWindow.nextBoundarySequence-oldWindow.nextBoundarySequence);
            sequenceDeltaRaw=raw;reason=TimingReason::CounterMismatch;
            const int32_t seq = raw < 32768 ? int32_t(raw) : int32_t(raw)-65536;
            const int64_t period = int64_t(s.awPeriod)*s.presenceMode*1024;
            // Two next-boundary estimates can straddle an EAW edge because the
            // on-wire down-counter is quantized in TU. Map equal AW counters.
            if (seq % s.presenceMode == 0 && seq >= -int32_t(s.presenceMode) && seq <= int32_t(s.presenceMode)) {
                const int64_t error = signedGp2Delta(newWindow.nextBoundaryGp2, oldWindow.nextBoundaryGp2) -
                    (seq / s.presenceMode)*period;
                if (error >= -0x7fffffffLL && error <= 0x7fffffffLL) residualUs = int32_t(error);
                else residualUs = 0x7fffffff;
                consistent = error >= -2048 && error <= 2048;
                reason=consistent ? TimingReason::Consistent : TimingReason::ResidualTooLarge;
            } else residualUs = 0x7fffffff;
        }
        streak = consistent ? (streak < 1000000 ? streak+1 : streak) : 1;
        previous=s; channels=c; receivedGp2=rx;txDelayUs=delay;
    }
};
struct PeerRocPlan {
    uint32_t boundaryGp2=0, waitUs=0, periodUs=0, observationAgeUs=0, streak=0;
    int32_t residualUs=0;
    uint32_t peerId=0, epoch=0; // anonymous identity and model lifetime for diagnostics
    uint8_t channel=0, slot=0;
};
inline bool planImminentPeerRoc(const TimingAudit &audit, const SyncParameters &sync,
                               const ChannelSequence &channels, uint32_t received, uint32_t now,
                               PeerRocPlan &out, uint32_t &reason) {
    out={};reason=10;
    if (audit.streak < 3 || !audit.consistent) return false;
    reason=11;
    WindowPrediction w={}; SlotView slot={};
    if (!predictWindowWithTxDelay(sync,received,now,500000,audit.txDelayUs,w) || !predictNextSlot(sync,channels,w,slot)) return false;
    reason=12;
    if (slot.channel!=6 && slot.channel!=44 && slot.channel!=149) return false;
    reason=13;
    // HOT_SPOT_CMD requires CURRENT GP2. Submit only when an eligible boundary
    // is imminent; no future timestamp, spinning, or additional high-rate timer.
    if (w.waitMicroseconds < 2000 || w.waitMicroseconds > 12000) return false;
    out.boundaryGp2=w.nextBoundaryGp2;out.waitUs=w.waitMicroseconds;
    out.periodUs=uint32_t(sync.awPeriod)*sync.presenceMode*1024;
    out.observationAgeUs=now-received;out.streak=audit.streak;out.residualUs=audit.residualUs;
    out.channel=slot.channel;out.slot=slot.slot;reason=0;return true;
}
struct RocAlignment { bool before=false;uint32_t distanceUs=0,overlapUs=0; };
inline RocAlignment measureRocAlignment(uint32_t start, uint32_t end, uint32_t boundary, uint32_t period) {
    RocAlignment out={};
    const int64_t offset=signedGp2Delta(start,boundary);
    out.before=offset<0;out.distanceUs=uint32_t(offset<0?-offset:offset);
    const uint32_t duration=end-start;
    if (!period || period>=0x80000000u || duration>65536) return out;
    const int64_t lo=offset>0?offset:0;
    const int64_t finish=offset+duration;
    const int64_t hi=finish<int64_t(period)?finish:period;
    if(hi>lo) out.overlapUs=uint32_t(hi-lo);
    return out;
}
}
