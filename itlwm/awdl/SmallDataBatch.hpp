// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
namespace ItlAwdl {
// Three serial packets, one original lease and one peer. Never retries a packet.
struct SmallDataBatch {
 static constexpr unsigned Count=3;
 bool enabled=false;unsigned index=0,submitted=0,completed=0,replied=0;
 uint64_t endNs=0;
 static uint64_t tokenAt(uint64_t root,unsigned ordinal){
  constexpr uint64_t max=0x7fffffffffffffffULL;
  if(!root||root>max||ordinal>=Count)return 0;
  return (root-1+ordinal)%max+1;
 }
 unsigned bit()const{return 1u<<index;}
 bool markSubmitted(){if(!enabled||index>=Count||(submitted&bit()))return false;submitted|=bit();return true;}
 bool markCompleted(){if(!enabled||index>=Count||!(submitted&bit())||(completed&bit()))return false;completed|=bit();return true;}
 bool markReply(){if(!enabled||index>=Count||!(submitted&bit())||(replied&bit()))return false;replied|=bit();return true;}
 bool mayAdvance(uint64_t now)const{
  return enabled&&index+1<Count&&endNs&&now<endNs&&
   (submitted&bit())&&(completed&bit())&&(replied&bit());
 }
 bool advance(uint64_t now){if(!mayAdvance(now))return false;++index;return true;}
 bool success()const{return enabled&&submitted==7&&completed==7&&replied==7;}
};
}
