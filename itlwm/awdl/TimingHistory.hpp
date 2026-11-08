// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
namespace ItlAwdl {
// Single RX-workloop writer, concurrent property readers. No allocation or MAC
// addresses. All words use SC atomics; a changed/busy row is skipped, never spun
// on or presented as a coherent observation. The whole array is not atomic.
struct TimingHistory {
    enum Field { Serial,PeerId,Epoch,ResetReason,AuditReason,Compared,Consistent,Streak,
        HostGapUs,ResetGapUs,GapUs,PreviousRxGp2,ReceiveGp2,PreviousAwSequence,AwSequence,
        PreviousDownCounter,DownCounter,PreviousAwPeriod,AwPeriod,PreviousPresenceMode,PresenceMode,
        PreviousBoundaryGp2,BoundaryGp2,SequenceDeltaRaw,ResidualRaw,RxChannel,ActionSubtype,
        ModelSlot,ModelChannel,ScheduleChangedSlot,AfPhyTx,AfTargetTx,Dot11Sequence,FrameLength,HostUsLow,HostUsHigh,Count };
    static constexpr unsigned Capacity=32;
    struct Row { uint32_t v[Count]={}; };
    struct Slot { uint32_t version=0;Row row; };
    Slot slots[Capacity];
    unsigned cursor=0;uint32_t serial=0;
    void append(const Row &input) {
        auto &slot=slots[cursor];
        __atomic_add_fetch(&slot.version,1u,__ATOMIC_SEQ_CST);
        if (++serial==0) ++serial;
        for(unsigned i=0;i<Count;++i)
            __atomic_store_n(&slot.row.v[i],i==Serial ? serial : input.v[i],__ATOMIC_SEQ_CST);
        __atomic_add_fetch(&slot.version,1u,__ATOMIC_SEQ_CST);
        cursor=(cursor+1)%Capacity;
    }
    bool read(unsigned index,Row &out) const {
        out={};if(index>=Capacity)return false;
        const auto &slot=slots[index];
        const uint32_t before=__atomic_load_n(&slot.version,__ATOMIC_SEQ_CST);
        if (!before || (before&1)) return false;
        Row candidate;
        for(unsigned i=0;i<Count;++i)candidate.v[i]=__atomic_load_n(&slot.row.v[i],__ATOMIC_SEQ_CST);
        const uint32_t after=__atomic_load_n(&slot.version,__ATOMIC_SEQ_CST);
        if(before!=after || !candidate.v[Serial])return false;
        out=candidate;return true;
    }
};
}
