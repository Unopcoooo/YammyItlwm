// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "NativeSyncRequest.hpp"
extern bool ItlNativeControlRoleAllowed(OSObject *controller,OSObject *owner,uint64_t epoch,bool entering);
extern bool ItlNativeWarmParamsAllowed(OSObject *,OSObject *,uint64_t);
// Configured host Master execution, distinct from peer sync, BSD readiness or airtime proof.
bool ItlIwm::awdlNativeSyncRoleReady(){
 if(!getMainWorkLoop()||!getMainWorkLoop()->inGate())return false;
 const auto &t=awdlNativeControlTx;
 const bool activeRole=t.phase!=ItlAwdl::NativeSearchControlTx::Failed&&
  t.phase!=ItlAwdl::NativeSearchControlTx::Cancelled&&!t.drainedAtSearchStop;
 return t.nativeRoleAccepted&&t.periodic&&activeRole&&itlAwdlRocNowNs()<t.leaseEndNs&&awdlNativeSearchEnabled()&&
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
 auto result=[](ItlIwm *hal,IOReturn status,uint32_t reason){
  if(auto *c=hal->getController())c->setProperty("AWDLNativeSyncRoleReason",reason,32);
  return status;
 };
 if(!payload||length!=24)return result(this,kIOReturnBadArgument,1);
 auto loop=getMainWorkLoop();auto gate=getMainCommandGate();if(!loop||!gate)return kIOReturnNotReady;
 ItlAwdlRocLifetime lifetime(this,gate,loop);
 if(!loop->inGate()){
  struct Request{ItlIwm *hal;void *payload;unsigned length;OSObject *owner;bool set;}r{this,payload,length,owner,set};
  return gate->runAction([](OSObject*,void *arg,void*,void*,void*)->IOReturn{
   auto r=static_cast<Request*>(arg);return r->hal->awdlNativeSyncRole(r->payload,r->length,r->owner,r->set);
  },&r);
 }
 auto &t=awdlNativeControlTx;
 if(!owner||owner!=ItlAwdl::gDataPort.owner||ItlAwdl::gDataPort.releasePending||dmaDetaching)
  return result(this,kIOReturnNotReady,2);
 if(!set){
  if(!awdlNativeSyncRoleReady())return result(this,t.nativeRoleAccepted?kIOReturnNotReady:kIOReturnUnsupported,3);
  if(!awdlNativeSyncRoleConfirmed())return result(this,kIOReturnNotReady,4);
  uint8_t out[24]={};out[4]=1;out[16]=1;memcpy(out+8,awdlNativeSearch.local,6);memcpy(payload,out,sizeof(out));
  return result(this,kIOReturnSuccess,0);
 }
 if(ItlAwdl::nativeLocalSyncRequest(payload,length,awdlNativeSearch.local))
  return result(this,kIOReturnUnsupported,5);
 const auto status=awdlNativeStartMaster(owner,ItlAwdl::NativeSyncProfile::advertisementPeriod);
 return result(this,status,status==kIOReturnSuccess?0:6);
}
IOReturn ItlIwm::awdlNativeStartMaster(OSObject *owner,uint32_t period,bool warmParams){
 auto &t=awdlNativeControlTx;
 auto root=getController();
 if(!root)return kIOReturnNotReady;
 const bool permitted=warmParams ? t.resumePriorMaster && ItlNativeWarmParamsAllowed(root,owner,awdlNativeSearch.epoch)
                                 : ItlNativeControlRoleAllowed(root,owner,awdlNativeSearch.epoch,true);
 if(!permitted){root->setProperty("AWDLNativeSyncRoleReason",10,32);return kIOReturnNotReady;}
 if(t.nativeRoleAccepted){const bool ready=awdlNativeSyncRoleReady();root->setProperty("AWDLNativeSyncRoleReason",ready?0:11,32);
  return ready?kIOReturnSuccess:kIOReturnNotReady;}
 // A prior manual diagnostic never becomes a native role by copying its state.
 if(t.phase!=ItlAwdl::NativeSearchControlTx::Idle){
  root->setProperty("AWDLNativeSyncRoleReason",12,32);return kIOReturnNotReady;}
 ItlIbssTxPort transport(this);ItlIbss::TxContext context={};
 if(!awdlNativeSearchEnabled()||!transport.snapshot(context)){
  root->setProperty("AWDLNativeSyncRoleReason",13,32);return kIOReturnNotReady;}
 const auto result=awdlNativeControlTxArm(awdlNativeSearch.epoch,true);
 if(result!=kIOReturnSuccess){root->setProperty("AWDLNativeSyncRoleReason",14,32);return result;}
 // The retained worker, actual radio/queue authority and unchanged lease are committed.
 t.schedule.psfPeriodTu=period;
 t.resumePriorMaster=false;
 t.nativeRoleAccepted=true;awdlNativeControlTxPublish();return kIOReturnSuccess;
}
