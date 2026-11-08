// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
namespace ItlAwdl {
inline bool rawRxTraceRequested(bool discovery,bool templateMode,bool requested){
    return requested&&(discovery||templateMode);
}
inline bool rawRxTraceInLease(bool templateMode,bool armed,int64_t deadline,uint64_t now){
    if(templateMode&&(!armed||deadline<=0))return false;
    return deadline<=0||now<=uint64_t(deadline)||now-uint64_t(deadline)<=2000000000ULL;
}
enum RawRxStage { RawIngress, RejectCrc, RejectLength, RejectPhy, RejectDecrypt,
                  RejectDuplicate, ReorderConsumed, CommonFrame, RejectBuffer, RawStageCount };
struct RawRxMetadata {
    uint32_t api=0,wireLength=0,length=0,status=0,channel=0,gp2=0;
    uint32_t macContext=0,macPhyIndex=0,filterMatch=0,frameControl=0;
    uint32_t rateFlags=0;bool rateValid=false;
    bool descriptorValid=false,lengthValid=false,statusValid=false,fcValid=false;
    uint8_t receiver[6]={},transmitter[6]={};bool addressValid=false;
};
inline uint32_t rawRx16(const uint8_t *p){return uint32_t(p[0])|(uint32_t(p[1])<<8);}
inline uint32_t rawRx32(const uint8_t *p){return rawRx16(p)|(rawRx16(p+2)<<16);}
// Internal address classification only; addresses are not serialized by the trace.
inline void rawRxAddresses(RawRxMetadata &out,const uint8_t *frame,size_t size){
    out.addressValid=false;
    if(!frame||size<24||(frame[0]&0x0c)!=8)return;
    for(unsigned i=0;i<6;++i){out.receiver[i]=frame[4+i];out.transmitter[i]=frame[10+i];}
    out.addressValid=true;
}
// Independent bounded observation of the wire payload, not the remaining DMA
// buffer capacity. No body is read; MAC addresses stay internal to peer classification.
inline RawRxMetadata decodeRawRx(const void *packet,size_t wireLength,bool mq,
                                uint32_t legacyChannel=0,uint32_t legacyGp2=0) {
    RawRxMetadata out;out.api=mq?2:1;out.wireLength=wireLength>UINT32_MAX?UINT32_MAX:uint32_t(wireLength);
    const auto p=static_cast<const uint8_t*>(packet);const size_t header=mq?48:4;
    if(!p || wireLength<header)return out;
    out.descriptorValid=true;out.length=rawRx16(p);
    out.lengthValid=out.length<=wireLength-header;
    if(mq) {
        out.status=rawRx16(p+12);out.statusValid=true;
        out.channel=p[34];out.gp2=rawRx32(p+36);out.macContext=p[35];
        out.macPhyIndex=p[7];out.filterMatch=rawRx32(p+24);
        out.rateFlags=rawRx32(p+28);out.rateValid=true;
    } else {
        out.channel=legacyChannel;out.gp2=legacyGp2;
        if(out.lengthValid && wireLength-header-out.length>=4) {
            out.status=rawRx32(p+header+out.length);out.statusValid=true;
        }
    }
    if(out.lengthValid && out.length>=2) {
        out.frameControl=rawRx16(p+header);out.fcValid=true;
        rawRxAddresses(out,p+header,out.length);
    }
    return out;
}
inline unsigned rawRxBucket(uint32_t channel) {
    return channel==6?1:channel==44?2:channel==149?3:channel?4:0;
}
}
