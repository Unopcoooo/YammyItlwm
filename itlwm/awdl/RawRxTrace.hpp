// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
namespace ItlAwdl {
// Single main-workloop gate writer, concurrent property readers. No allocation or MAC
// addresses. All words use SC atomics; a changed/busy row is skipped, never spun
// on or presented as a coherent observation. The whole array is not atomic.
struct RawRxTrace {
    enum Field { Serial,Stage,Api,WireLength,MPDULength,DescriptorValid,LengthValid,StatusValid,Status,Channel,RxGp2,MacContext,MacPhyIndex,FilterMatch,FrameControl,FcValid,Generation,HostUsLow,HostUsHigh,Attempt,Phase,TargetChannel,HomeChannel,Uid,StartGp2,EndGp2,DurationTu,Discovery,ScanFlags,ScanSubmitting,NetState,NextNetState,RxControl,Count };
    static constexpr unsigned Capacity=2048;
    static const char *fieldName(unsigned index) {
        static const char *const names[]={"Serial","Stage","Api","WireLength","MPDULength","DescriptorValid","LengthValid","StatusValid","Status","Channel","RxGp2","MacContext","MacPhyIndex","FilterMatch","FrameControl","FcValid","Generation","HostUsLow","HostUsHigh","Attempt","Phase","TargetChannel","HomeChannel","Uid","StartGp2","EndGp2","DurationTu","Discovery","ScanFlags","ScanSubmitting","NetState","NextNetState","RxControl"};
        static_assert(sizeof(names)/sizeof(names[0])==Count,"RX trace names");
        return index<Count ? names[index] : "Unknown";
    }
    struct Row { uint32_t v[Count]={}; };
    struct RecordSlot { uint32_t version=0;Row row; };
    RecordSlot slots[Capacity];
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
