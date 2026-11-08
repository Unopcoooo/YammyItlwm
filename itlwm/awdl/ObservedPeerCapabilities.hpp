// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "ActionFrame.hpp"
#include <string.h>
namespace ItlAwdl {
struct ObservedPeerCapabilities {
 uint16_t ht=0;uint8_t ampdu=0;uint32_t mcs=0,generation=0;uint64_t observedNs=0;
 bool fresh(uint32_t gen,uint64_t now)const{
  return generation&&generation==gen&&observedNs&&now>=observedNs&&now-observedNs<=500000000ULL&&(mcs&255);
 }
 uint32_t stationFlags(unsigned localStreams)const{
  const unsigned smps=(ht>>2)&3;
  const bool two=localStreams>=2&&(mcs&0xff00)&&smps!=0;
  return (two?1u<<28:0u)|(smps==1?1u<<17:0u)|
   (uint32_t(ampdu&3)<<19)|(uint32_t((ampdu>>2)&7)<<23);
 }
 // AWDL HT TLV 7: reserved(2), HT(2), A-MPDU(1), MCS(1..4), reserved(2).
 // Never derive WMM, aggregation admission or keys from this HT-only field.
 bool observe(const uint8_t *wire,size_t n,const uint8_t peer[6],uint32_t gen,uint64_t now){
  Summary summary;if(!gen||!now||inspect(wire,n,summary)!=Result::Valid||memcmp(wire+10,peer,6))return false;
  size_t at=24+((wire[1]&0x80)?4:0)+16;bool found=false;ObservedPeerCapabilities next;
  while(at<n){const size_t len=size_t(wire[at+1])|(size_t(wire[at+2])<<8);const auto v=wire+at+3;
   if(wire[at]==7){
    if(found||len<8||len>11||v[0]||v[1]||v[len-1]||v[len-2]||(v[4]&0xe0))return false;
    found=true;next.ht=uint16_t(v[2])|(uint16_t(v[3])<<8);next.ampdu=v[4];
    for(size_t i=0;i<len-7;++i)next.mcs|=uint32_t(v[5+i])<<(8*i);
    if(!(next.mcs&255)||((next.ht>>2)&3)==2)return false;
   }
   at+=len+3;
  }
  if(!found|| (generation==gen&&now<observedNs))return false;
  next.generation=gen;next.observedNs=now;*this=next;return true;
 }
};
}
