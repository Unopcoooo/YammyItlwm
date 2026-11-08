// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "Descriptor.hpp"
#include "ListenerDescriptor.hpp"
#include "SingleTx.hpp"
namespace ItlIbss {
struct TxContext {
    Reservation resources={};uint32_t generation=0;unsigned slot=0;
    uint64_t now=0,leaseEnd=0;uint8_t antenna=0;unsigned coexistPriority=0;
    bool healthy=false,ringEmpty=false;
};
// Transport contract: all entries run in one gate. prepareBuffer may allocate
// and temporarily release that gate, after copying the input to its own
// immutable frame/body storage; cancelled preparation must return before
// its local buffer can be discarded. revalidate proves the FULL original
// reservation, generation, ring slot, channel permission, lease and hardware
// state again. stage is non-failing/non-sleeping, installs mbuf/ring metadata,
// command+TFD and scheduler duplicate before advance+DMA barrier. doorbell is
// the first firmware-visible publication; an unknown MMIO outcome stays owned.
// stopProof requires real device-stop+DMA+notification-drain proof for this
// retired context. Generation changes/flush replies alone are never proof.
// This template has no IOKit implementation and is not a live TX adapter.
template<class Transport,bool Monitor=false,bool Mdns=false> class SingleFrameAdapter {
    static_assert(!(Monitor&&Mdns),"monitor and IBSS mDNS are different routes");
public:
    explicit SingleFrameAdapter(Transport &transport,bool mdns=Mdns):t_(transport),mdns_(mdns){}
    SingleFrameAdapter(const SingleFrameAdapter&)=delete;
    SingleFrameAdapter &operator=(const SingleFrameAdapter&)=delete;
    enum class Completion {Unowned,Consume,Reclaim,Defer};
    bool start(const uint8_t *frame,size_t length) {
        if((Monitor&&mdns_) || !t_.inGate() || started_ || stopped_ || !frame || length<40 || length>2304)return reject(1);
        started_=true;
        if(!t_.snapshot(context_))return reject(2);
        const bool accepted=Monitor?tx_.prepareMonitor(context_.resources,context_.generation,context_.slot,
            context_.now,context_.leaseEnd,context_.healthy,context_.ringEmpty):tx_.prepare(context_.resources,context_.generation,context_.slot,
            context_.now,context_.leaseEnd,context_.healthy,context_.ringEmpty);
        if(!accepted)return reject(3);
        preparing_=true;
        const bool prepared=t_.prepareBuffer(frame,length,buffer_);
        preparing_=false;
        uint64_t now=0;
        if(!prepared){discard();return reject(4);}
        if(stopped_ || !t_.revalidate(context_,now) || now<context_.now || now>=context_.leaseEnd) {
            discard();return reject(5);
        }
        const uint64_t validatedNow=now;
        iwm_device_cmd command;
        iwm_tfd tfd;uint16_t scheduler=0;
        if(buffer_.length!=length || !(mdns_?mdnsDescriptor:(Monitor?listenerDescriptor:descriptor))(context_.resources,buffer_.frame,buffer_.length,buffer_.commandAddress,buffer_.scratchAddress,
            buffer_.segments,buffer_.count,context_.slot,context_.antenna,context_.coexistPriority,2000,
            command,tfd,scheduler)) {discard();return reject(6);}
        // No gate-release operation between this revalidation, ownership
        // transfer and publication. Local construction has no device effects.
        if(!t_.revalidate(context_,now) || now<validatedNow || now>=context_.leaseEnd) {discard();return reject(7);}
        const uint64_t remaining=context_.leaseEnd-now;
        const uint64_t deadline=now+(remaining<2000000000ull?remaining:2000000000ull);
        if(stopped_ || !tx_.publish(context_.generation,now,deadline,true)){discard();return reject(8);}
        published_=true;
        t_.stage(context_,buffer_,command,tfd,scheduler);
        t_.doorbell(context_);
        return true;
    }
    Completion complete(uint32_t generation,unsigned queue,unsigned slot) {
        if(!t_.inGate())return Completion::Defer; // caller must preserve and queue notification
        auto outcome=tx_.complete(generation,queue,slot);
        if(outcome==SingleTx::Completion::Reclaim){reclaim();return Completion::Reclaim;}
        return outcome==SingleTx::Completion::Unowned?Completion::Unowned:Completion::Consume;
    }
    void cancel() {
        if(!t_.inGate())return;
        stopped_=true;tx_.cancel();
        if(!preparing_)flushIfNeeded();
    }
    void tick() {
        if(!t_.inGate() || preparing_)return;
        tx_.poll(t_.generation(),t_.now());
        if(tx_.stopped()){stopped_=true;flushIfNeeded();}
    }
    bool hardwareStopped() {
        if(!t_.inGate() || preparing_ || !published_ || !t_.stopProof(context_))return false;
        stopped_=true;
        if(tx_.deviceQuiesced()==SingleTx::Completion::Reclaim)reclaim();
        return true;
    }
    uint64_t pendingDeadline()const{return tx_.pendingDeadline();}
    bool pending()const{return tx_.pending();}
private:
    bool reject(uint32_t stage){t_.record("AWDLIbssTxRejectStage",stage);return false;}
    void discard(){tx_.cancel();stopped_=true;t_.discardBuffer(buffer_);}
    void reclaim(){t_.reclaim(context_,buffer_);}
    void flushIfNeeded() {
        if(!tx_.pending())return;
        const uint64_t now=t_.now();if(now>UINT64_MAX-2000000000ull)return;
        uint32_t mask=0;
        if(tx_.requestFlush(t_.generation(),now,now+2000000000ull,mask))
            if(!t_.flush(mask))tx_.flushFailed();
    }
    Transport &t_;const bool mdns_;TxContext context_;typename Transport::Buffer buffer_={};SingleTx tx_;
    bool started_=false,stopped_=false,preparing_=false,published_=false;
};
}
