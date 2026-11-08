// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "DataPolicy.hpp"
namespace ItlAwdl {
struct SingleDataRequest {uint8_t sourceIp[16]={},peerIp[16]={},peer[6]={};uint64_t token=0;};
enum class SingleDataPhase:uint32_t {Unused,Armed,Preparing,Submitted,Completed,Aborted,Expired};
inline bool probeLinkLocal(const uint8_t *ip){
    if(!ip||ip[0]!=0xfe||ip[1]!=0x80)return false;
    for(unsigned i=2;i<8;++i)if(ip[i])return false;
    uint8_t any=0;for(unsigned i=8;i<16;++i)any|=ip[i];return any!=0;
}
inline uint16_t probeChecksum(const uint8_t *ip){
    uint32_t sum=24+58;
    for(unsigned i=8;i<40;i+=2)sum+=dataBe16(ip+i);
    for(unsigned i=40;i<64;i+=2)sum+=dataBe16(ip+i);
    while(sum>>16)sum=(sum&65535)+(sum>>16);
    return uint16_t(~sum);
}
inline bool buildSingleDataEcho(const SingleDataRequest &r,const uint8_t local[6],uint32_t generation,
                                 uint16_t sequence,uint8_t *out,size_t capacity,size_t &length){
    length=0;
    if(!r.token||!generation||!dataUnicast(local)||!dataUnicast(r.peer)||!memcmp(local,r.peer,6)||
       !probeLinkLocal(r.sourceIp)||!probeLinkLocal(r.peerIp)||!memcmp(r.sourceIp,r.peerIp,16))return false;
    uint8_t eth[78]={};memcpy(eth,r.peer,6);memcpy(eth+6,local,6);eth[12]=0x86;eth[13]=0xdd;
    auto ip=eth+14;ip[0]=0x60;ip[5]=24;ip[6]=58;ip[7]=64;
    memcpy(ip+8,r.sourceIp,16);memcpy(ip+24,r.peerIp,16);
    auto icmp=ip+40;icmp[0]=128;icmp[4]=uint8_t(r.token>>8);icmp[5]=uint8_t(r.token);
    icmp[6]=uint8_t(generation>>8);icmp[7]=uint8_t(generation);
    memcpy(icmp+8,"ADP1",4);for(unsigned i=0;i<8;++i)icmp[12+i]=uint8_t(r.token>>(56-8*i));
    for(unsigned i=0;i<4;++i)icmp[20+i]=uint8_t(generation>>(24-8*i));
    const uint16_t checksum=probeChecksum(ip);icmp[2]=uint8_t(checksum>>8);icmp[3]=uint8_t(checksum);
    return encodeData(eth,sizeof(eth),local,sequence,sequence,out,capacity,length)==DataError::Ok;
}
struct SingleDataProbe {
    SingleDataRequest request;SingleDataPhase phase=SingleDataPhase::Unused;
    bool everArmed=false,pending=false,cancelled=false,ackReported=false,replyObserved=false,retired=false;
    uint8_t local[6]={},frame[104]={};uint32_t generation=0,modelEpoch=0,submitted=0,completed=0;
    uint32_t lastBlockReason=0,publishedBlockReason=UINT32_MAX;
    unsigned queue=0,index=0;uint32_t status=UINT32_MAX,reason=0,sentGp2=0,replyGp2=0;
    uint64_t deadlineNs=0,sentNs=0;
    bool arm(const SingleDataRequest &r,const uint8_t source[6],uint32_t gen,uint64_t now,uint64_t engineEnd){
        uint8_t test[104];size_t length;
        if(everArmed||engineEnd<=now||engineEnd-now<500000000ULL||
           !buildSingleDataEcho(r,source,gen,0,test,sizeof(test),length))return false;
        request=r;memcpy(local,source,6);generation=gen;everArmed=true;phase=SingleDataPhase::Armed;
        deadlineNs=engineEnd-now>30000000000ULL?now+30000000000ULL:engineEnd;return true;
    }
    void tick(uint32_t gen,uint64_t now){
        if(phase==SingleDataPhase::Armed&&(gen!=generation||now>=deadlineNs)){
            phase=gen==generation?SingleDataPhase::Expired:SingleDataPhase::Aborted;reason=1;
        }
    }
    void cancel(){cancelled=true;if(phase==SingleDataPhase::Armed||phase==SingleDataPhase::Preparing)phase=SingleDataPhase::Aborted;}
    void quiesced(){if(phase==SingleDataPhase::Armed||phase==SingleDataPhase::Preparing||pending)phase=SingleDataPhase::Aborted;pending=false;retired=everArmed;}
    bool blocked(uint32_t code){reason=lastBlockReason=code;return false;}
    const Peer *freshPeer(const DataContext &c){
        if(!dataRadioContextReady(c)){blocked(31);return nullptr;}
        for(unsigned i=0;i<c.peers->count;++i){const auto &p=c.peers->peers[i];
            if(memcmp(p.address,request.peer,6))continue;
            if(!p.hasSync||!p.hasChannels||!p.timing.consistent||p.timing.streak<3){blocked(34);return nullptr;}
            if(c.gp2-p.lastSeenGp2>500000||c.gp2-p.syncReceivedGp2>500000||
               p.timing.receivedGp2!=p.syncReceivedGp2){blocked(35);return nullptr;}
            return &p;
        }
        blocked(33);return nullptr;
    }
    bool eligible(const DataContext &c){
        tick(c.generation,c.nowNs);
        if(phase!=SingleDataPhase::Armed&&phase!=SingleDataPhase::Preparing)return false;
        if(c.nowNs>=deadlineNs||(phase==SingleDataPhase::Preparing&&modelEpoch!=c.modelEpoch)||cancelled||c.generation!=generation)return blocked(30);
        const auto peer=freshPeer(c);if(!peer)return false;
        if(!dataActualRadioWindow(c,true))return blocked(32);
        const auto &p=*peer;
        if(!p.sync.awPeriod||p.sync.awPeriod>64||!p.sync.presenceMode||p.sync.presenceMode>4||
           uint32_t(p.sync.awPeriod)*p.sync.presenceMode>64)return blocked(36);
        WindowPrediction window;SlotView slot;
        if(!predictWindowWithTxDelay(p.sync,p.syncReceivedGp2,c.gp2,500000,p.timing.txDelayUs,window))return blocked(36);
        const uint32_t period=uint32_t(p.sync.awPeriod)*p.sync.presenceMode*1024;
        if(!period||period>65536||window.waitMicroseconds<5000||
           uint32_t(c.gp2-(window.nextBoundaryGp2-period))<2048)return blocked(37);
        window.nextBoundarySequence=uint16_t(window.nextBoundarySequence-p.sync.presenceMode);
        if(!predictNextSlot(p.sync,p.channels,window,slot)||slot.channel!=6)return blocked(38);
        if((p.channels.encoding!=1&&p.channels.encoding!=3)||
           p.channels.entries[slot.slot].auxiliary!=(p.channels.encoding==1?0x2b:0x51))return blocked(38);
        reason=0;return true;
    }
    bool begin(uint32_t gen,uint32_t epoch,uint64_t now){
        tick(gen,now);if(phase!=SingleDataPhase::Armed||cancelled||!epoch)return false;
        modelEpoch=epoch;phase=SingleDataPhase::Preparing;return true;
    }
    void failed(uint32_t why){if(phase==SingleDataPhase::Preparing){phase=SingleDataPhase::Aborted;reason=why;}}
    bool publish(uint32_t gen,unsigned q,unsigned slot,uint32_t gp2,uint64_t now,const uint8_t *bytes,size_t length){
        if(phase!=SingleDataPhase::Preparing||cancelled||generation!=gen||now>=deadlineNs||length!=sizeof(frame))return false;
        queue=q;index=slot;sentGp2=gp2;sentNs=now;memcpy(frame,bytes,length);
        pending=true;++submitted;phase=SingleDataPhase::Submitted;return true;
    }
    bool complete(uint32_t gen,unsigned q,unsigned slot,uint32_t raw,bool ack){
        if(!pending||generation!=gen||queue!=q||index!=slot)return false;
        pending=false;status=raw;ackReported=ack;++completed;phase=SingleDataPhase::Completed;return true;
    }
    bool reply(const uint8_t *wire,size_t n,uint32_t gen,uint32_t channel,uint32_t gp2,uint64_t now){
        if(!submitted||retired||replyObserved||generation!=gen||channel!=6||now<sentNs||now-sentNs>5000000000ULL||
           uint32_t(gp2-sentGp2)>5000000||(n!=104&&n!=106))return false;
        uint8_t eth[78];size_t length;uint16_t seq;
        if(decodeData(wire,n,local,request.peer,eth,sizeof(eth),length,seq)!=DataError::Ok||length!=sizeof(eth))return false;
        const auto ip=eth+14;const auto icmp=ip+40;
        if(ip[6]!=58||memcmp(ip+8,request.peerIp,16)||memcmp(ip+24,request.sourceIp,16)||
           icmp[0]!=129||icmp[1]||probeChecksum(ip)!=0||memcmp(icmp+4,frame+84,20))return false;
        replyObserved=true;replyGp2=gp2;return true;
    }
};
extern SingleDataProbe gSingleDataProbe;
}
