// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "DataFrames.hpp"
namespace ItlAwdl {
// Read-only view. Never remove bytes from an mbuf or treat a failed CRC as valid FCS.
inline size_t observerFrameLength(const uint8_t *p,size_t n,bool expectFcs,bool &verified){
    verified=false;if(!expectFcs||!p||n<4||n>4096)return n;
    uint32_t crc=UINT32_MAX;
    for(size_t i=0;i<n-4;++i){crc^=p[i];for(unsigned b=0;b<8;++b)crc=(crc>>1)^(0xedb88320u&uint32_t(0-int32_t(crc&1)));}
    const uint32_t tail=uint32_t(p[n-4])|(uint32_t(p[n-3])<<8)|(uint32_t(p[n-2])<<16)|(uint32_t(p[n-1])<<24);
    verified=(~crc)==tail;return verified?n-4:n;
}
enum TargetRxMetric {TargetFrames,TargetNoMsdu,TargetProtected,TargetFcsVerified,TargetFcsUnverified,
 TargetLayoutRejected,TargetProtocolRejected,TargetLengthRejected,TargetAddressRejected,TargetIpv6,
 TargetIcmpChecksumRejected,TargetEchoRequest,TargetEchoReply,TargetStimulusMatched,TargetRxMetricCount};
struct TargetRxObserver {
    uint32_t counts[TargetRxMetricCount]={};uint16_t lastIdentifier=0,lastSequence=0;
    void count(TargetRxMetric k){if(counts[k]<UINT32_MAX)++counts[k];}
    void observe(const uint8_t *p,size_t n,const uint8_t *local,const uint8_t *peer,const uint8_t *peerIp,uint64_t token,bool expectFcs){
        if(!p||!local||!peer||!peerIp||n<24||n>4096||(p[0]&12)!=8||memcmp(p+10,peer,6))return;
        count(TargetFrames);bool verified=false;n=observerFrameLength(p,n,expectFcs,verified);
        if(expectFcs)count(verified?TargetFcsVerified:TargetFcsUnverified);
        if(n<24){count(TargetLengthRejected);return;}
        const auto fc=dataLe16(p);
        if(fc&0x40){count(TargetNoMsdu);return;}
        if(fc&0x4000){count(TargetProtected);return;}
        uint8_t eth[kEtherDataMax];size_t size=0;uint16_t sequence=0;
        const auto error=decodeData(p,n,local,peer,eth,sizeof(eth),size,sequence);
        if(error!=DataError::Ok){count(error==DataError::Length?TargetLengthRejected:(error==DataError::Protocol?TargetProtocolRejected:((error==DataError::Address||error==DataError::Peer)?TargetAddressRejected:TargetLayoutRejected)));return;}
        count(TargetIpv6);const auto ip=eth+14;const auto payload=ip+40;const size_t len=size-14-40;
        if(ip[6]!=58||len<8)return;
        uint32_t sum=uint32_t(len)+58;
        for(unsigned i=8;i<40;i+=2)sum+=dataBe16(ip+i);
        for(size_t i=0;i<len;i+=2)sum+=uint32_t(payload[i])<<8|(i+1<len?payload[i+1]:0);
        while(sum>>16)sum=(sum&65535)+(sum>>16);
        if(uint16_t(sum)!=65535){count(TargetIcmpChecksumRejected);return;}
        if(payload[1])return;
        if(payload[0]==128)count(TargetEchoRequest);else if(payload[0]==129)count(TargetEchoReply);else return;
        if(!token||payload[0]!=128||len!=56||ip[7]!=1||memcmp(ip+8,peerIp,16)||dataBe16(payload+6)>=20)return;
        const uint8_t allNodes[16]={0xff,2,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
        if(memcmp(ip+24,allNodes,16))return;
        uint8_t marker[16]={'A','W','D','L','S','T','I','M'};
        for(unsigned i=0;i<8;++i)marker[8+i]=uint8_t(token>>(56-8*i));
        for(size_t i=8;i+sizeof(marker)<=len;++i)if(!memcmp(payload+i,marker,sizeof(marker))){
            count(TargetStimulusMatched);lastIdentifier=dataBe16(payload+4);lastSequence=dataBe16(payload+6);break;
        }
    }
};
}
