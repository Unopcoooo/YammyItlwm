// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "DataFrames.hpp"
namespace ItlAwdl {
enum class DataPhase { Free, Queued, InFlight };
struct DataTicket { uint64_t serial=0;uint32_t generation=0;unsigned slot=0; };
struct DataCompletion { void *packet=nullptr,*queue=nullptr;DataTicket ticket; };
struct DataTransfer { uint8_t bytes[kAwdlDataMax];size_t length=0;DataTicket ticket; };
// Caller serializes through its command gate. No native object is accessed here.
// Packet and queue references belong to the adapter until completion is returned.
// The adapter must authorize the peer/channel and recheck the window before DMA.
struct DataQueue {
    static constexpr unsigned capacity=4;
    struct Slot {
        DataPhase phase=DataPhase::Free;DataTransfer frame;
        void *packet=nullptr,*queue=nullptr;
    } slots[capacity];
    uint64_t serial=0,deadlineNs=0;uint32_t generation=0;
    uint8_t local[6]={};bool enabled=false;
    bool empty()const {for(const auto &s:slots)if(s.phase!=DataPhase::Free)return false;return true;}
    bool open(uint32_t gen,const uint8_t address[6],uint64_t now,uint64_t deadline){
        if(enabled||!empty()||!gen||!dataUnicast(address)||deadline<=now||deadline-now>300000000000ULL)return false;
        generation=gen;memcpy(local,address,6);deadlineNs=deadline;enabled=true;return true;
    }
    bool ready(uint32_t gen,uint64_t now)const{return enabled&&gen==generation&&now<deadlineNs;}
    bool push(const uint8_t *eth,size_t n,uint32_t gen,uint64_t now,uint16_t wifiSeq,uint16_t awdlSeq,
              void *packet,void *queue,DataTicket &ticket){
        ticket={};if(!packet||!queue)return false;
        return pushFrame(eth,n,gen,now,wifiSeq,awdlSeq,packet,queue,ticket);
    }
    // BSD output owns an independent byte copy, with no Skywalk handle to recycle.
    bool pushCopied(const uint8_t *eth,size_t n,uint32_t gen,uint64_t now,uint16_t wifiSeq,uint16_t awdlSeq,DataTicket &ticket){
        return pushFrame(eth,n,gen,now,wifiSeq,awdlSeq,nullptr,nullptr,ticket);
    }
private:
    bool pushFrame(const uint8_t *eth,size_t n,uint32_t gen,uint64_t now,uint16_t wifiSeq,uint16_t awdlSeq,
                   void *packet,void *queue,DataTicket &ticket){
        ticket={};if(!ready(gen,now)||serial==UINT64_MAX)return false;
        unsigned index=capacity;for(unsigned i=0;i<capacity;++i)if(slots[i].phase==DataPhase::Free){index=i;break;}
        if(index==capacity)return false;
        auto &s=slots[index];size_t length;
        if(encodeData(eth,n,local,wifiSeq,awdlSeq,s.frame.bytes,sizeof(s.frame.bytes),length)!=DataError::Ok)return false;
        ticket={++serial,gen,index};s.frame.length=length;s.frame.ticket=ticket;
        s.packet=packet;s.queue=queue;s.phase=DataPhase::Queued;return true;
    }
public:
    bool take(uint32_t gen,uint64_t now,DataTransfer &out){
        if(!ready(gen,now))return false;
        Slot *oldest=nullptr;
        for(auto &s:slots)if(s.phase==DataPhase::Queued&&(!oldest||s.frame.ticket.serial<oldest->frame.ticket.serial))oldest=&s;
        if(!oldest)return false;
        out=oldest->frame;oldest->phase=DataPhase::InFlight;return true;
    }
    bool finish(DataTicket ticket,DataCompletion &out){
        out={};if(ticket.slot>=capacity)return false;
        auto &s=slots[ticket.slot];
        if(s.phase!=DataPhase::InFlight||ticket.serial!=s.frame.ticket.serial||ticket.generation!=s.frame.ticket.generation)return false;
        out.packet=s.packet;out.queue=s.queue;out.ticket=ticket;
        s.packet=s.queue=nullptr;s.frame.length=0;s.phase=DataPhase::Free;return true;
    }
    void stop(){enabled=false;}
    // Return one reference pair at a time. Hardware-owned packets require proof
    // that DMA is quiesced; expiry alone is never permission to free them.
    bool reclaim(bool dmaQuiesced,DataCompletion &out){
        out={};if(enabled)return false;
        for(auto &s:slots){
            if(s.phase==DataPhase::Free||(s.phase==DataPhase::InFlight&&!dmaQuiesced))continue;
            out.packet=s.packet;out.queue=s.queue;out.ticket=s.frame.ticket;
            s.packet=s.queue=nullptr;s.frame.length=0;s.phase=DataPhase::Free;return true;
        }
        return false;
    }
};
// False leaves the original Skywalk packet unconsumed. Only the extra references
// acquired here are released on refusal; accepted packets await real completion.
template<class Ops> bool offerDataPacket(DataQueue &q,typename Ops::Packet *packet,typename Ops::Queue *queue,
                                        uint32_t gen,uint64_t now,uint16_t wifiSeq,uint16_t awdlSeq){
    if(!packet||!queue||!q.ready(gen,now)||Ops::bufferCount(packet)!=1)return false;
    const size_t length=Ops::length(packet);if(length<54||length>kEtherDataMax)return false;
    const auto bytes=Ops::bytes(packet);if(!bytes)return false;
    Ops::retain(packet,queue);DataTicket ticket;
    if(q.push(bytes,length,gen,now,wifiSeq,awdlSeq,packet,queue,ticket))return true;
    Ops::release(packet,queue);return false;
}
template<class Ops> int completeDataPacket(DataCompletion &done,int status){
    if(!done.packet||!done.queue)return 0;
    auto packet=static_cast<typename Ops::Packet *>(done.packet);
    auto queue=static_cast<typename Ops::Queue *>(done.queue);
    done={}; // Consume before native callbacks, including reentrant completion.
    const int result=Ops::complete(packet,queue,status);Ops::release(packet,queue);return result;
}
}
