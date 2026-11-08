// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "NativeSyncRequest.hpp"
extern bool ItlNativeControlRoleAllowed(OSObject *controller,OSObject *owner,uint64_t epoch,bool entering);
extern bool ItlNativeWarmParamsAllowed(OSObject *,OSObject *,uint64_t);
// Configured host Master execution, distinct from peer sync, BSD readiness or airtime proof.
bool ItlIwm::awdlNativeSyncRoleReady(){
 if(!getMainWorkLoop()||!getMainWorkLoop()->inGate())return false;
 const auto &t=awdlNativeControlTx;
 const bool quiet=t.phase==ItlAwdl::NativeSearchControlTx::Complete&&
  t.roleQuietUntilNs&&itlAwdlRocNowNs()<t.roleQuietUntilNs;
 return t.nativeRoleAccepted&&t.periodic&&(t.active()||quiet)&&awdlNativeSearchEnabled()&&
  awdlNativeSearch.matches(t.epoch)&&t.generation==awdlNativeSearch.generation&&
  t.generation==uint32_t(com.sc_generation)&&t.leaseEndNs==awdlNativeSearch.deadlineNs&&
  t.radioLeaseEndNs==awdlIbssExclusive.deadline&&
  ItlNativeControlRoleAllowed(getController(),ItlAwdl::gDataPort.owner,t.epoch,false);
}
bool ItlIwm::awdlNativeSyncRoleConfirmed(){
 if(!awdlNativeSyncRoleReady())return false;
 const auto &s=awdlNativeControlTx.schedule;
 const auto psf=s.psf-unsigned(s.pending&&s.inFlight==ItlAwdl::BoundedControlSchedule::Psf);
 const auto mif=s.mif-unsigned(s.pending&&s.inFlight==ItlAwdl::BoundedControlSchedule::Mif);
 return psf&&mif&&awdlNativeControlTx.completed>=2;
}
IOReturn ItlIwm::awdlNativeSyncRole(void *payload,unsigned length,OSObject *owner,bool set){
 if(!payload||length!=24)return kIOReturnBadArgument;
 auto loop=getMainWorkLoop();auto gate=getMainCommandGate();if(!loop||!gate)return kIOReturnNotReady;
 ItlAwdlRocLifetime lifetime(this,gate,loop);
 if(!loop->inGate()){
  struct Request{ItlIwm *hal;void *payload;unsigned length;OSObject *owner;bool set;}r{this,payload,length,owner,set};
  return gate->runAction([](OSObject*,void *arg,void*,void*,void*)->IOReturn{
   auto r=static_cast<Request*>(arg);return r->hal->awdlNativeSyncRole(r->payload,r->length,r->owner,r->set);
  },&r);
 }
 auto &t=awdlNativeControlTx;
 if(!owner||owner!=ItlAwdl::gDataPort.owner||ItlAwdl::gDataPort.releasePending||dmaDetaching)return kIOReturnNotReady;
 if(!set){
  if(!awdlNativeSyncRoleReady())return t.nativeRoleAccepted?kIOReturnNotReady:kIOReturnUnsupported;
  if(!awdlNativeSyncRoleConfirmed())return kIOReturnNotReady;
  uint8_t out[24]={};out[4]=1;out[16]=1;memcpy(out+8,awdlNativeSearch.local,6);memcpy(payload,out,sizeof(out));
  return kIOReturnSuccess;
 }
 if(ItlAwdl::nativeLocalSyncRequest(payload,length,awdlNativeSearch.local))return kIOReturnUnsupported;
 return awdlNativeStartMaster(owner,ItlAwdl::NativeSyncProfile::advertisementPeriod);
}
IOReturn ItlIwm::awdlNativeStartMaster(OSObject *owner,uint32_t period,bool warmParams){
 auto &t=awdlNativeControlTx;
 auto root=getController();
 if(!root)return kIOReturnNotReady;
 const bool permitted=warmParams ? t.resumePriorMaster && ItlNativeWarmParamsAllowed(root,owner,awdlNativeSearch.epoch)
                                 : ItlNativeControlRoleAllowed(root,owner,awdlNativeSearch.epoch,true);
 if(!permitted)return kIOReturnNotReady;
 if(t.nativeRoleAccepted)return awdlNativeSyncRoleReady()?kIOReturnSuccess:kIOReturnNotReady;
 // A prior manual diagnostic never becomes a native role by copying its state.
 if(t.phase!=ItlAwdl::NativeSearchControlTx::Idle)return kIOReturnNotReady;
 ItlIbssTxPort transport(this);ItlIbss::TxContext context={};
 if(!awdlNativeSearchEnabled()||!transport.snapshot(context))return kIOReturnNotReady;
 const auto result=awdlNativeControlTxArm(awdlNativeSearch.epoch,true);if(result!=kIOReturnSuccess)return result;
 // The retained worker, actual radio/queue authority and unchanged lease are committed.
 t.schedule.psfPeriodTu=period;
 t.resumePriorMaster=false;
 t.nativeRoleAccepted=true;awdlNativeControlTxPublish();return kIOReturnSuccess;
}
