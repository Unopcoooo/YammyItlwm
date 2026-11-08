// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "DataFrames.hpp"
namespace ItlAwdl {
inline bool nativePeerIdentity(const uint8_t mac[6],const uint8_t ip[16]) {
 if(!mac||!ip||!dataUnicast(mac)||ip[0]!=0xfe||ip[1]!=0x80)return false;
 for(unsigned i=2;i<8;++i)if(ip[i])return false;
 const uint8_t iid[8]={uint8_t(mac[0]^2),mac[1],mac[2],0xff,0xfe,mac[3],mac[4],mac[5]};
 return memcmp(ip+8,iid,8)==0;
}
struct NativePeerKey {
 uint8_t mac[6]={},ip[16]={};uint32_t generation=0,epoch=0;
 bool valid()const{return generation&&epoch&&nativePeerIdentity(mac,ip);}
};
// Node notice completion is not proof of route ownership or removal. The
// explicit experiment requires an empty scoped mapping on admission and
// independent kernel-table readback after withdrawal.
template<class Object> struct NativePeerRegistration {
 Object *owner=nullptr;void *interface=nullptr;NativePeerKey key;
 bool busy=false,cancelled=false,attempted=false,active=false;
 int presentResult=0,absentResult=0;unsigned presents=0,withdrawals=0;
 bool idle()const{return !owner&&!busy;}
 template<class Ops> bool rollback(Ops &ops){
  if(!owner)return true;
  const auto current=ops.bound(owner);
  if(current&&current!=interface)return false;
  if(attempted){++withdrawals;absentResult=ops.absent(interface,key);if(absentResult)return false;attempted=false;}
  auto oldOwner=owner;auto oldInterface=interface;
  owner=nullptr;interface=nullptr;active=false;key={};
  ops.releaseInterface(oldInterface);oldOwner->release();return true;
 }
 template<class Ops> bool start(Ops &ops,Object *object,void *ifnet,const NativePeerKey &input){
  if(!idle()||!object||!ifnet||!input.valid()||ops.bound(object)!=ifnet||!ops.retainInterface(ifnet))return false;
  object->retain();owner=object;interface=ifnet;key=input;busy=true;cancelled=false;active=false;attempted=true;
  ++presents;presentResult=ops.present(ifnet,key);
  const bool ok=!presentResult&&!cancelled&&ops.bound(object)==ifnet;
  if(ok)active=true;else rollback(ops);
  busy=false;return ok;
 }
 template<class Ops> bool stop(Ops &ops){
  cancelled=true;if(busy)return false;
  busy=true;const bool ok=rollback(ops);busy=false;return ok;
 }
};
}
