// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "TargetRxObserver.hpp"
namespace ItlAwdl {
enum PairRxMetric {PairFrames,PairNoMsdu,PairProtected,PairFcsVerified,PairFcsUnverified,
 PairDecodeRejected,PairIpRejected,PairChecksumRejected,PairMarkerRejected,PairIdentifierRejected,
 PairEchoRequests,PairEchoReplies,PairMatchedFrames,PairDuplicates,PairRxMetricCount};
inline bool pairLinkLocal(const uint8_t *ip){
 if(!ip||ip[0]!=0xfe||ip[1]!=0x80)return false;
 for(unsigned i=2;i<8;++i)if(ip[i])return false;
 uint8_t any=0;for(unsigned i=8;i<16;++i)any|=ip[i];return any;
}
// Third-party observation only: the configured receiver is passed to the
// existing decoder as its intended destination. No production decoder rule,
// mbuf, interface identity, packet delivery or hardware ACK policy is changed.
struct ListenerPairObserver {
 uint8_t mac[2][6]={},ip[2][16]={};uint64_t token=0;bool configured=false,identifierKnown=false;
 uint32_t counts[PairRxMetricCount]={},seen[4]={},lastCode=0,lastPayloadHash=0;
 uint16_t identifier=0,lastSequence=0;
 void count(PairRxMetric m){if(counts[m]<UINT32_MAX)++counts[m];}
 bool configure(const uint8_t *a,const uint8_t *aIp,const uint8_t *b,const uint8_t *bIp,
                const uint8_t *host,uint64_t nonce){
  if(configured||!nonce||!dataUnicast(a)||!dataUnicast(b)||!dataUnicast(host)||
     !memcmp(a,b,6)||!memcmp(a,host,6)||!memcmp(b,host,6)||
     !pairLinkLocal(aIp)||!pairLinkLocal(bIp)||!memcmp(aIp,bIp,16))return false;
  memcpy(mac[0],a,6);memcpy(mac[1],b,6);memcpy(ip[0],aIp,16);memcpy(ip[1],bIp,16);
  token=nonce;configured=true;return true;
 }
 // Returns 1..4 only on a new, exact (direction,type,sequence) match.
 unsigned observe(const uint8_t *p,size_t n,bool expectFcs){
  if(!configured||!p||n<24||n>4096||(p[0]&12)!=8)return 0;
  unsigned from=0;
  if(!memcmp(p+10,mac[0],6)&&!memcmp(p+4,mac[1],6))from=0;
  else if(!memcmp(p+10,mac[1],6)&&!memcmp(p+4,mac[0],6))from=1;
  else return 0;
  count(PairFrames);bool verified=false;n=observerFrameLength(p,n,expectFcs,verified);
  if(expectFcs)count(verified?PairFcsVerified:PairFcsUnverified);
  const auto fc=dataLe16(p);
  if(fc&0x40){count(PairNoMsdu);return 0;}if(fc&0x4000){count(PairProtected);return 0;}
  uint8_t eth[kEtherDataMax];size_t size=0;uint16_t awdl=0;
  if(decodeData(p,n,mac[1-from],mac[from],eth,sizeof(eth),size,awdl)!=DataError::Ok){count(PairDecodeRejected);return 0;}
  const auto v6=eth+14;const auto echo=v6+40;const size_t length=size-54;
  if(memcmp(v6+8,ip[from],16)||memcmp(v6+24,ip[1-from],16)||v6[6]!=58||!v6[7]||length!=56){count(PairIpRejected);return 0;}
  uint32_t sum=56+58;for(unsigned i=8;i<40;i+=2)sum+=dataBe16(v6+i);
  for(unsigned i=0;i<56;i+=2)sum+=dataBe16(echo+i);
  while(sum>>16)sum=(sum&65535)+(sum>>16);
  if(uint16_t(sum)!=65535){count(PairChecksumRejected);return 0;}
  if(echo[1]||(echo[0]!=128&&echo[0]!=129))return 0;
  count(echo[0]==128?PairEchoRequests:PairEchoReplies);
  uint8_t marker[16]={'A','W','D','L','S','N','I','F'};
  for(unsigned i=0;i<8;++i)marker[8+i]=uint8_t(token>>(56-8*i));
  bool found=false;for(unsigned i=8;i+sizeof(marker)<=56;++i)if(!memcmp(echo+i,marker,sizeof(marker))){found=true;break;}
  const auto seq=dataBe16(echo+6);if(!found||seq>=3){count(PairMarkerRejected);return 0;}
  const auto id=dataBe16(echo+4);
  if(identifierKnown&&identifier!=id){count(PairIdentifierRejected);return 0;}
  identifierKnown=true;identifier=id;count(PairMatchedFrames);
  const unsigned kind=from*2+(echo[0]==129?1:0);const uint32_t bit=1u<<seq;
  if(seen[kind]&bit){count(PairDuplicates);return 0;}
  seen[kind]|=bit;lastCode=kind+1;lastSequence=seq;lastPayloadHash=2166136261u;
  for(unsigned i=8;i<56;++i)lastPayloadHash=(lastPayloadHash^echo[i])*16777619u;
  return lastCode;
 }
};
}
