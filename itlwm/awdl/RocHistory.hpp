// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
namespace ItlAwdl {
// Single main-workloop gate writer, concurrent property readers. No allocation or MAC
// addresses. All words use SC atomics; a changed/busy row is skipped, never spun
// on or presented as a coherent observation. The whole array is not atomic.
struct RocHistory {
    enum Field { Serial,Attempt,Kind,PeerId,Epoch,Generation,Channel,BoundaryGp2,PeriodUs,Slot,ObservationAgeUs,Streak,ResidualRaw,PlanGp2,RequestedGp2,MaxDelayTu,Phase,Uid,StartGp2,EndGp2,Status,ResponseStatus,Before,DistanceUs,OverlapUs,NotificationLength,Command0,Command1,Command2,Command3,Command4,Command5,Command6,Command7,Command8,Command9,Command10,Notification0,Notification1,Notification2,Notification3,Notification4,Notification5,Discovery,DurationTu,RxFrames,AwdlFrames,BeaconFrames,OffHomeFrames,HomeChannel,Count };
    static constexpr unsigned Capacity=256;
    static const char *fieldName(unsigned index) {
        static const char *const names[]={"Serial","Attempt","Kind","PeerId","Epoch","Generation","Channel","BoundaryGp2","PeriodUs","Slot","ObservationAgeUs","Streak","ResidualRaw","PlanGp2","RequestedGp2","MaxDelayTu","Phase","Uid","StartGp2","EndGp2","Status","ResponseStatus","Before","DistanceUs","OverlapUs","NotificationLength","Command0","Command1","Command2","Command3","Command4","Command5","Command6","Command7","Command8","Command9","Command10","Notification0","Notification1","Notification2","Notification3","Notification4","Notification5","Discovery","DurationTu","RxFrames","AwdlFrames","BeaconFrames","OffHomeFrames","HomeChannel"};
        static_assert(sizeof(names)/sizeof(names[0])==Count,"ROC history names");
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
