// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "NativeSyncProfile.hpp"
// The selected period drives actual frame construction and retained worker cadence.
IOReturn ItlIwm::awdlNativeSyncParams(void *payload,unsigned length,OSObject *owner,bool set){
 if(!payload||length!=24)return kIOReturnBadArgument;
 auto loop=getMainWorkLoop();auto gate=getMainCommandGate();if(!loop||!gate)return kIOReturnNotReady;
 ItlAwdlRocLifetime lifetime(this,gate,loop);
 if(!loop->inGate()){
  struct Request{ItlIwm *hal;void *payload;unsigned length;OSObject *owner;bool set;}r{this,payload,length,owner,set};
  return gate->runAction([](OSObject*,void *arg,void*,void*,void*)->IOReturn{
   auto r=static_cast<Request*>(arg);return r->hal->awdlNativeSyncParams(r->payload,r->length,r->owner,r->set);
  },&r);
 }
 if(!owner||owner!=ItlAwdl::gDataPort.owner||ItlAwdl::gDataPort.releasePending||dmaDetaching)return kIOReturnNotReady;
 using P=ItlAwdl::NativeSyncProfile;
 auto &t=awdlNativeControlTx;
 const uint32_t actual[]={0,P::awLength,P::awPeriod,P::awExtension,t.schedule.psfPeriodTu,0};
 auto p=static_cast<uint8_t*>(payload);
 const bool ready=awdlNativeSyncRoleReady();
 const bool warm=set && t.resumePriorMaster && t.phase==ItlAwdl::NativeSearchControlTx::Idle;
 if(!ready&&!warm)return kIOReturnNotReady;
 if(set){
  uint32_t requestedPeriod=0;
  for(unsigned n=0;n<6;++n){const unsigned off=n*4;
   const uint32_t v=uint32_t(p[off])|uint32_t(p[off+1])<<8|uint32_t(p[off+2])<<16|uint32_t(p[off+3])<<24;
   if(n==4){if(!P::supportsAdvertisementPeriod(v))return kIOReturnUnsupported;requestedPeriod=v;}
   else if(v!=actual[n])return kIOReturnUnsupported;
  }
  // A configured PeerManager may program SYNC_PARAMS first on link-up.
  // Rebuild the retired local Master only inside the new pinned publication.
  if(warm)return awdlNativeStartMaster(owner,requestedPeriod,true);
  // Quiet/completed roles cannot schedule new work or silently accept a change.
  if(requestedPeriod!=actual[4]){
   if(!t.active()||!t.schedule.setPsfPeriod(t.generation,itlAwdlRocNowNs(),requestedPeriod))return kIOReturnNotReady;
   getMainCommandGate()->commandWakeup(&awdlIbssWorkPending,false);
  }
  return kIOReturnSuccess;
 }
 if(!awdlNativeSyncRoleConfirmed())return kIOReturnNotReady;
 uint8_t out[24]={};
 for(unsigned n=0;n<6;++n)for(unsigned b=0;b<4;++b)out[n*4+b]=uint8_t(actual[n]>>(8*b));
 memcpy(payload,out,sizeof(out));return kIOReturnSuccess;
}
