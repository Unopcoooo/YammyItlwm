// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "DataNative.hpp"
#include "DataPolicy.hpp"
#include "NativeQueueActivation.hpp"
#include <sys/errno.h>
namespace ItlAwdl {
template<class Object> struct DataPort {
    DataQueue tx;
    NativeQueueActivation<Object> queueActivation;
    struct HardwareFlight {
        DataTicket ticket;unsigned queue=0,index=0;bool pending=false;
    } hardware;
    Object *owner=nullptr,*pool=nullptr,*txQueue=nullptr,*rxQueue=nullptr;
    uint8_t registeredLocal[6]={};
    uint8_t nativeSource[16]={};
    bool nativeSession=false;
    bool nativeAutoPeer=false;
    uint8_t nativePeerMAC[6]={},nativePeerIP[16]={};
    uint32_t nativePeerGeneration=0,nativePeerEpoch=0;
    bool nativeStopPending=false;
    bool releasePending=false;
    uint32_t txNotifyWindow=0;bool txNotifyValid=false;
    using LinkAction=bool (*)(Object *,bool);
    LinkAction linkAction=nullptr;
    using BsdInputAction=int (*)(Object *,const uint8_t *,size_t);
    BsdInputAction bsdInputAction=nullptr;
    bool (*controlGuard)(Object *,bool start)=nullptr;
    bool linkUp=false,linkBusy=false,linkDirectionUp=false,linkDownPending=false;
    uint16_t wifiSequence=0,awdlSequence=0;
    struct Received {uint8_t bytes[kEtherDataMax];size_t length=0;uint32_t epoch=0;uint64_t hostNs=0;} rx[4];
    unsigned rxHead=0,rxCount=0;uint32_t rxEpoch=0,modelEpoch=0;
    struct Seen {uint8_t source[6]={};uint16_t sequence=0;uint64_t bits=0;} seen[kMaxPeers];
    bool bind(Object *o,Object *p,Object *t,Object *r,const uint8_t *address=nullptr){
        if(owner||!tx.empty()||!o||!p||!t||!r||(address&&!dataUnicast(address)))return false;
        o->retain();owner=o;pool=p;txQueue=t;rxQueue=r;nativeStopPending=false;releasePending=false;
        if(address)memcpy(registeredLocal,address,6);else memset(registeredLocal,0,6);memset(nativeSource,0,16);
        return true;
    }
    bool open(uint32_t gen,const uint8_t local[6],uint64_t now,uint64_t end){
        if(!owner||(controlGuard&&!controlGuard(owner,true))||!tx.open(gen,local,now,end))return false;
        modelEpoch=0;return true;
    }
    // Main-gate transaction, after the HAL has prepared real radio/peer state.
    // No callbacks/firmware waits between validation and admission commit.
    bool openNative(const DataContext &prepared,const uint8_t current[6],uint64_t end){
        if(!owner||releasePending||tx.enabled||!tx.empty()||hardware.pending||rxCount||nativeSession||
           !current||!dataUnicast(registeredLocal)||memcmp(current,registeredLocal,6)||
           prepared.radioKind!=DataRadioKind::ExclusiveIbss||!dataRadioContextReady(prepared)||
           memcmp(prepared.ibssView()->local,registeredLocal,6)||
           end<=prepared.nowNs||end>prepared.deadlineNs||end-prepared.nowNs<500000000ULL||end-prepared.nowNs>2500000000ULL||
           !nativeDataQueuesReady(txQueue,rxQueue))return false;
        if(!open(prepared.generation,registeredLocal,prepared.nowNs,end))return false;
        auto committed=prepared;committed.deadlineNs=end;committed.backendReady=true;
        if(!ready(committed)){stop(false,ECANCELED);return false;}
        txNotifyValid=false;nativeStopPending=false;nativeSession=true;return true;
    }
    bool ready(const DataContext &c){
        if(!owner||!dataContextReady(c)||!tx.ready(c.generation,c.nowNs)||c.deadlineNs>tx.deadlineNs)return false;
        if(modelEpoch&&modelEpoch!=c.modelEpoch){stop(false,ECANCELED);return false;}
        modelEpoch=c.modelEpoch;return true;
    }
    bool publishNativeLink(bool up){
        if(linkBusy){if(!up&&linkDirectionUp)linkDownPending=true;return false;}
        if(!up&&!linkUp&&!linkDownPending)return true;
        if(!owner||!linkAction||(up&&(!nativeSession||linkDownPending)))return false;
        if(up&&linkUp)return true;
        linkBusy=true;linkDirectionUp=up;
        if(!up){linkUp=false;linkDownPending=false;}
        const bool result=linkAction(owner,up);linkBusy=false;
        if(!up){if(!result)linkDownPending=true;return result;}
        if(result&&nativeSession&&!linkDownPending){linkUp=true;return true;}
        // Even a failed native publication may have changed some interface state.
        linkDownPending=true;publishNativeLink(false);return false;
    }
    unsigned offer(Object *o,Object *queue,Object *const *packets,unsigned count,const DataContext &c){
        if(o!=owner||queue!=txQueue||!packets||!ready(c))return 0;
        unsigned accepted=0;
        for(;accepted<count;++accepted){
            if(o!=owner||queue!=txQueue||!ready(c))break;
            auto p=packets[accepted];
            if(!NativeDataOps<Object>::valid(p,queue))break;
            const size_t n=dataNativeLength(p);
            const auto bytes=dataNativeBufferCount(p)==1?NativeDataOps<Object>::bytes(p):nullptr;
            if(!bytes||validateDataEthernet(bytes,n,tx.local)!=DataError::Ok){
                NativeDataOps<Object>::retain(p,queue);DataCompletion bad;bad.packet=p;bad.queue=queue;
                completeDataPacket<NativeDataOps<Object>>(bad,EINVAL);continue;
            }
            if(nativeSession&&c.radioKind==DataRadioKind::ExclusiveIbss&&(bytes[0]&1)){
                // This bounded prototype supports one unicast peer, not NDP/mDNS.
                // Consume unsupported packets with an error so they cannot block
                // valid unicast packets behind them in the system submission ring.
                NativeDataOps<Object>::retain(p,queue);DataCompletion bad;bad.packet=p;bad.queue=queue;
                completeDataPacket<NativeDataOps<Object>>(bad,EOPNOTSUPP);continue;
            }
            if(nativeSession&&c.radioKind==DataRadioKind::ExclusiveIbss&&memcmp(bytes,c.ibssView()->peer,6)){
                NativeDataOps<Object>::retain(p,queue);DataCompletion bad;bad.packet=p;bad.queue=queue;
                completeDataPacket<NativeDataOps<Object>>(bad,EHOSTUNREACH);continue;
            }
            if(!dataDestinationAllowed(bytes,tx.local,c)||
               !offerNativeDataPacket(tx,p,queue,c.generation,c.nowNs,wifiSequence,awdlSequence))break;
            wifiSequence=uint16_t((wifiSequence+1)&4095);++awdlSequence;
        }
        return accepted; // Consumed prefix includes packets completed with error.
    }
    bool take(const DataContext &c,DataTransfer &frame){
        if(hardware.pending)return false;
        if(!ready(c))return false;
        const DataTransfer *oldest=nullptr;
        for(auto &s:tx.slots)if(s.phase==DataPhase::Queued&&(!oldest||s.frame.ticket.serial<oldest->ticket.serial))oldest=&s.frame;
        return oldest&&dataDestinationAllowed(oldest->bytes+4,tx.local,c)&&tx.take(c.generation,c.nowNs,frame);
    }
    bool finish(DataTicket ticket,int status,int &diagnostic){
        if(hardware.pending)return false; // Only the matching hardware completion owns this packet.
        DataCompletion done;if(!tx.finish(ticket,done))return false;
        diagnostic=completeDataPacket<NativeDataOps<Object>>(done,status);return true;
    }
    // The native transport supplies its reserved queue/current slot before DMA
    // publication. Software queue slots and hardware ring indices are distinct.
    bool takeForHardware(const DataContext &c,unsigned queue,unsigned index,DataTransfer &frame){
        if(hardware.pending||queue<10||queue>17||index>=256||!take(c,frame))return false;
        hardware.ticket=frame.ticket;hardware.queue=queue;hardware.index=index;hardware.pending=true;return true;
    }
    bool finishHardware(uint32_t generation,unsigned queue,unsigned index,int status,int &diagnostic){
        if(!hardware.pending||generation!=hardware.ticket.generation||queue!=hardware.queue||index!=hardware.index)return false;
        const auto ticket=hardware.ticket;hardware={}; // Consume before reentrant native recycling.
        return finish(ticket,status,diagnostic);
    }
    // Failed publication is only safe to retire when the transport proves it
    // never handed this descriptor to hardware. A partial submit must await stop.
    bool abortBeforeHardwarePublication(bool neverPublished,int status,int &diagnostic){
        if(!neverPublished||!hardware.pending)return false;
        return finishHardware(hardware.ticket.generation,hardware.queue,hardware.index,status,diagnostic);
    }
    void expire(uint32_t gen,uint64_t now,int status){
        if(tx.enabled&&(tx.generation!=gen||now>=tx.deadlineNs))stop(false,status);
    }
    // Disable admission before calling native completion, which may reenter.
    void stop(bool dmaQuiesced,int status){
        queueActivation.requestStop();
        nativeStopPending|=nativeSession;
        nativeSession=false;
        if(dmaQuiesced)hardware={};
        tx.stop();publishNativeLink(false);rxCount=rxHead=0;rxEpoch=0;for(auto &s:seen)s={};
        DataCompletion done;while(tx.reclaim(dmaQuiesced,done))completeDataPacket<NativeDataOps<Object>>(done,status);
    }
    bool unbind(){
        if(owner&&controlGuard&&!controlGuard(owner,false))return false;
        if(tx.enabled||!tx.empty()||rxCount||hardware.pending||!queueActivation.idle()||linkUp||linkBusy||linkDownPending)return false;
        auto oldOwner=owner;owner=pool=txQueue=rxQueue=nullptr;memset(registeredLocal,0,6);memset(nativeSource,0,16);nativeStopPending=false;releasePending=false;
        nativeAutoPeer=false;nativePeerGeneration=nativePeerEpoch=0;memset(nativePeerMAC,0,6);memset(nativePeerIP,0,16);
        linkAction=nullptr;bsdInputAction=nullptr;controlGuard=nullptr;if(oldOwner)oldOwner->release();return true;
    }
    bool receive(const uint8_t *wire,size_t n,const DataContext &c){
        if(!ready(c)||!dataActualWindow(c,false)||!wire||n<24)return false;
        const Peer *peer=nullptr;
        for(unsigned i=0;i<c.peers->count;++i)if(!memcmp(c.peers->peers[i].address,wire+10,6)){peer=&c.peers->peers[i];break;}
        if(!peer||!dataPeerAvailable(*peer,tx.local,c,(wire[4]&1)!=0))return false;
        if(rxEpoch!=c.modelEpoch){rxCount=rxHead=0;for(auto &s:seen)s={};rxEpoch=c.modelEpoch;}
        if(rxCount==4)return false;
        auto &out=rx[(rxHead+rxCount)%4];uint16_t sequence;size_t length;
        if(decodeData(wire,n,tx.local,peer->address,out.bytes,sizeof(out.bytes),length,sequence)!=DataError::Ok)return false;
        Seen *history=nullptr;
        for(auto &s:seen)if(s.bits&&!memcmp(s.source,peer->address,6)){history=&s;break;}
        if(!history)for(auto &s:seen)if(!s.bits){history=&s;break;}
        if(!history)for(auto &s:seen){
            bool live=false;for(unsigned i=0;i<c.peers->count;++i)live|=!memcmp(s.source,c.peers->peers[i].address,6);
            if(!live){s={};history=&s;break;}
        }
        if(!history)return false;
        if(history->bits){
            const uint16_t advance=uint16_t(sequence-history->sequence);
            if(advance&&advance<32768){history->bits=advance>=64?1:(history->bits<<advance)|1;history->sequence=sequence;}
            else {const uint16_t back=uint16_t(history->sequence-sequence);
                if(back>=64||(history->bits&(1ULL<<back)))return false;history->bits|=1ULL<<back;}
        }else{memcpy(history->source,peer->address,6);history->sequence=sequence;history->bits=1;}
        out.length=length;out.epoch=c.modelEpoch;out.hostNs=c.nowNs;++rxCount;return true;
    }
    unsigned poll(Object *o,Object *queue,Object **packets,unsigned capacity,const DataContext &c){
        if(bsdInputAction||o!=owner||queue!=rxQueue||!packets||!ready(c))return 0;
        unsigned produced=0;
        while(rxCount&&produced<capacity){auto &front=rx[rxHead];
            if(front.epoch!=c.modelEpoch||c.nowNs<front.hostNs||c.nowNs-front.hostNs>500000000ULL){rxHead=(rxHead+1)%4;--rxCount;continue;}
            auto packet=allocateNativeDataReceive(pool,front.bytes,front.length);if(!packet)break;
            packets[produced++]=packet;rxHead=(rxHead+1)%4;--rxCount;
        }
        return produced;
    }
    unsigned deliverBsd(const DataContext &c){
        if(!bsdInputAction||!owner||!nativeSession||!ready(c))return 0;
        unsigned count=0;
        while(rxCount&&count<4){
            auto &front=rx[rxHead];
            const bool fresh=front.epoch==c.modelEpoch&&c.nowNs>=front.hostNs&&c.nowNs-front.hostNs<=500000000ULL;
            uint8_t bytes[kEtherDataMax];const auto length=front.length;
            if(fresh)memcpy(bytes,front.bytes,length);
            // Remove before input: a synchronous consumer must not see this frame twice.
            rxHead=(rxHead+1)%4;--rxCount;
            if(fresh){bsdInputAction(owner,bytes,length);++count;}
            if(!owner||!nativeSession||!bsdInputAction||!ready(c))break;
        }
        return count;
    }
    int notifyReceive(){
        // Notification neither transfers packet ownership nor drains this queue.
        if(!owner||!tx.enabled||!rxCount)return -1;
        return notifyNativeDataReceive(rxQueue);
    }
    int notifyTransmit(){
        if(!owner||!tx.enabled||!nativeSession)return -1;
        return notifyNativeDataTransmit(txQueue);
    }
};
}
