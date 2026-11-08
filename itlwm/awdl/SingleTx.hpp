// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "ManagementRoute.hpp"
#include "AuxTx.hpp"
namespace ItlIbss {
// Single gate writer; one frame per object/lease. No reset/rearm API: preserve
// queue/slot tombstone until actual stop, so duplicates never fall into STA free.
class SingleTx {
public:
    enum class Completion {Unowned,Consume,Reclaim};
    bool prepare(const Reservation &r,uint32_t generation,unsigned slot,uint64_t now,uint64_t leaseEnd,bool healthy,bool ringEmpty) {
        ManagementRoute route;
        if(used_ || !managementRoute(r,0xd0,true,route) || !generation || slot>=256 ||
           !healthy || !ringEmpty || leaseEnd<=now || leaseEnd-now>20000000000ull)return false;
        used_=prepared_=true;generation_=generation;queue_=route.queue;slot_=slot;lease_=leaseEnd;last_=now;return true;
    }
    bool prepareMonitor(const Reservation &r,uint32_t generation,unsigned slot,uint64_t now,uint64_t leaseEnd,bool healthy,bool ringEmpty){
        if(used_||(!r.valid()||r.mac!=1||r.phy!=1||r.tsf!=1||r.mcastSta!=3||r.bcastSta!=4)||!generation||slot>=256||!healthy||!ringEmpty||leaseEnd<=now||leaseEnd-now>20000000000ull)return false;
        used_=prepared_=true;generation_=generation;queue_=2;slot_=slot;lease_=leaseEnd;last_=now;return true;
    }
    bool publish(uint32_t generation,uint64_t now,uint64_t completionDeadline,bool healthy) {
        if(!prepared_ || stopped_ || generation!=generation_ || now<last_ || now>=lease_ ||
           !healthy || completionDeadline<=now || completionDeadline>lease_)return false;
        if(!owner_.attempt() || !owner_.submit(generation_,queue_,slot_,completionDeadline))return false;
        last_=now;prepared_=false;return true;
    }
    Completion complete(uint32_t generation,unsigned queue,unsigned slot) {
        if(!used_ || queue!=queue_ || slot!=slot_)return Completion::Unowned;
        if(generation!=generation_ || !owner_.pending)return Completion::Consume;
        owner_.complete(generation,queue,slot);return Completion::Reclaim;
    }
    void cancel(){stopped_=true;prepared_=false;}
    bool requestFlush(uint32_t generation,uint64_t now,uint64_t cleanupDeadline,uint32_t &mask) {
        if(!stopped_ || generation!=generation_ || cleanupDeadline<=now || cleanupDeadline-now>2000000000ull)return false;
        if(!owner_.requestFlush(generation,cleanupDeadline))return false;
        mask=uint32_t(1)<<queue_;return true;
    }
    void poll(uint32_t generation,uint64_t now) {
        if(!used_)return;
        if(generation!=generation_ || now<last_ || now>=lease_ || (owner_.pending && now>=owner_.deadlineNs)){
            cancel();if(owner_.pending && owner_.flushSent)owner_.quarantine();
        }
        if(now>=last_)last_=now;
    }
    void flushFailed(){cancel();if(owner_.pending)owner_.quarantine();}
    // Caller must have completed the real hardware stop AND DMA-quiescence
    // proof. A flush reply or generation change alone must never call this.
    Completion deviceQuiesced(){cancel();const bool held=owner_.pending;owner_.deviceQuiesced();return held?Completion::Reclaim:Completion::Consume;}
    uint64_t pendingDeadline()const{return owner_.deadlineNs;}
    bool pending()const{return owner_.pending;}
    bool stopped()const{return stopped_;}
private:
    ItlAwdl::AuxTxOwnership owner_;
    bool used_=false,prepared_=false,stopped_=false;
    uint32_t generation_=0;unsigned queue_=0,slot_=0;uint64_t lease_=0,last_=0;
};
}
