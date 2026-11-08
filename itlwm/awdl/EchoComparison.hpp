// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "SingleDataProbe.hpp"
namespace ItlAwdl {
// Explicit IBSS comparisons only. Profile 0 preserves the original probe.
// 1: pinned Owly neighbor echo; 2: captured native Mac IPv6; 3: same IPv6, QoS TID0.
struct EchoComparison {
 unsigned profile=0;uint8_t expected[106]={};size_t expectedLength=0;
 static uint16_t checksum(const uint8_t *ip,size_t n){
  uint32_t sum=uint32_t(n-40)+58;
  for(unsigned i=8;i<40;i+=2)sum+=dataBe16(ip+i);
  for(size_t i=40;i<n;i+=2)sum+=uint16_t(uint16_t(ip[i])<<8)|(i+1<n?ip[i+1]:0);
  while(sum>>16)sum=(sum&65535)+(sum>>16);return uint16_t(~sum);
 }
 bool build(const SingleDataRequest &r,const uint8_t *local,uint32_t gen,uint16_t seq,
            uint8_t *out,size_t cap,size_t &used)const{
  used=0;if(!profile)return buildSingleDataEcho(r,local,gen,seq,out,cap,used);
  if(profile>3||!r.token||!gen||!dataUnicast(local)||!dataUnicast(r.peer)||!memcmp(local,r.peer,6)||!probeLinkLocal(r.sourceIp)||!probeLinkLocal(r.peerIp)||
     !memcmp(r.sourceIp,r.peerIp,16))return false;
  // Native fixture: apple-positive-control/native-capture/apple.pcap, first request.
  const uint8_t nativePayload[]={0x6a,0xe1,0xed,0x99,0,0x0a,0x35,0x0f};
  uint8_t eth[70]={};memcpy(eth,r.peer,6);memcpy(eth+6,local,6);eth[12]=0x86;eth[13]=0xdd;
  const size_t payload=profile==1?4:8,ipLength=48+payload;
  auto ip=eth+14;ip[0]=0x60;
  if(profile!=1){ip[1]=0x0f;ip[2]=2;} // Preserve captured traffic class/flow label.
  ip[5]=uint8_t(8+payload);ip[6]=58;ip[7]=profile==1?255:64;
  memcpy(ip+8,r.sourceIp,16);memcpy(ip+24,r.peerIp,16);
  auto icmp=ip+40;icmp[0]=128;
  // Per-run identifier/sequence replace captured values to prevent stale matches.
  icmp[4]=uint8_t(r.token>>8);icmp[5]=uint8_t(r.token);icmp[6]=uint8_t(gen>>8);icmp[7]=uint8_t(gen);
  memcpy(icmp+8,profile==1?static_cast<const void*>("owly"):static_cast<const void*>(nativePayload),payload);
  const auto sum=checksum(ip,ipLength);icmp[2]=uint8_t(sum>>8);icmp[3]=uint8_t(sum);
  const size_t need=40+ipLength+(profile==3?2:0);if(!out||cap<need)return false;
  if(encodeData(eth,14+ipLength,local,seq,seq,out,cap,used)!=DataError::Ok)return false;
  if(profile==3){memmove(out+26,out+24,used-24);out[0]=0x88;out[24]=out[25]=0;used+=2;}
  return true;
 }
 bool prepare(const SingleDataRequest &r,const uint8_t *local,uint32_t gen){
  expectedLength=0;return profile&&build(r,local,gen,1,expected,sizeof(expected),expectedLength);
 }
 bool reply(const uint8_t *wire,size_t n,const SingleDataRequest &r,const uint8_t *local)const{
  if(!profile||!expectedLength)return false;
  uint8_t eth[70];size_t length=0;uint16_t seq=0;
  if(decodeData(wire,n,local,r.peer,eth,sizeof(eth),length,seq)!=DataError::Ok)return false;
  const size_t ipLength=profile==1?52:56;
  if(length!=14+ipLength)return false;
  const auto ip=eth+14;const auto icmp=ip+40;const auto want=expected+(profile==3?42:40)+40;
  return ip[6]==58&&ip[7]&& !memcmp(ip+8,r.peerIp,16)&&!memcmp(ip+24,r.sourceIp,16)&&
   icmp[0]==129&&!icmp[1]&&checksum(ip,ipLength)==0&&!memcmp(icmp+4,want+4,ipLength-44);
 }
};
}
