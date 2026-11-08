// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "NativeSyncRequest.hpp"
#include "SelfControlAdvertisement.hpp"
// Observation only. Never allocates a DMA buffer, publishes a descriptor or accepts a role.
void ItlIwm::awdlNativeSyncStatePreflight(const void *payload,unsigned length){
 auto loop=getMainWorkLoop();auto gate=getMainCommandGate();if(!loop||!gate)return;
 ItlAwdlRocLifetime lifetime(this,gate,loop);
 if(!loop->inGate()){
  struct Request{ItlIwm *hal;const void *payload;unsigned length;}r{this,payload,length};
  gate->runAction([](OSObject*,void *arg,void*,void*,void*)->IOReturn{
   auto r=static_cast<Request*>(arg);r->hal->awdlNativeSyncStatePreflight(r->payload,r->length);return kIOReturnSuccess;
  },&r);return;
 }
 unsigned reason=0,radio=0;uint32_t channel=0;bool sampled=false,frames=false;
 ItlIbss::TxContext context={};ItlAwdl::SelfControlInput input;
 ItlAwdl::SelfControlEvidence psfEvidence,mifEvidence;size_t psfLength=0,mifLength=0;
 uint8_t psf[512]={},mif[512]={};
 if((reason=ItlAwdl::nativeLocalSyncRequest(payload,length,awdlNativeSearch.local))){}
 else if(!awdlNativeSearchMasterChannel(channel))reason=5;
 else if(awdlIbssTxPort||awdlIbssDataActive()||ItlAwdl::gDataPort.nativeSession)reason=6;
 else if((radio=ItlIbssTxPort::radioRejection(this,true)))reason=7;
 else{
  ItlIbssTxPort transport(this);
  if(!transport.snapshot(context))reason=8;
  else{
   const auto before=itlAwdlRocNowNs();
   if(!iwm_nic_lock(&com))reason=9;
   else{
    const auto gp2=iwm_read_prph(&com,IWM_DEVICE_SYSTEM_TIME_REG);iwm_nic_unlock(&com);
    const auto after=itlAwdlRocNowNs();
    if(after<before||after-before>2000000ULL)reason=10;
    else{
     sampled=true;memcpy(input.source,awdlNativeSearch.local,6);
     input.generation=awdlNativeSearch.generation;input.currentGeneration=uint32_t(com.sc_generation);
     input.epochGp2=input.nowGp2=gp2;input.epochHostNs=before+(after-before)/2;input.nowHostNs=after;
     input.leaseEndNs=awdlNativeSearch.deadlineNs;
     input.subtype=0;const bool psfOK=ItlAwdl::buildSelfControlAdvertisement(input,psf,sizeof(psf),psfLength,psfEvidence);
     input.subtype=3;const bool mifOK=ItlAwdl::buildSelfControlAdvertisement(input,mif,sizeof(mif),mifLength,mifEvidence);
     uint8_t local[6]={};
     frames=psfOK&&mifOK&&awdlIbssLocalAddress(local)&&!memcmp(local,input.source,6)&&
      !memcmp(psf+10,local,6)&&!memcmp(mif+10,local,6)&&
      !memcmp(psf+16,awdlIbssExclusive.session.mac.bssid,6)&&!memcmp(mif+16,awdlIbssExclusive.session.mac.bssid,6);
     if(!frames)reason=11;
    }
   }
  }
 }
 auto root=getController();auto d=OSDictionary::withCapacity(14);if(!root||!d){if(d)d->release();return;}
 const char *keys[]={"Version","Epoch","Generation","Reason","RadioRejection","Channel","Sampled","FramesBuilt","PSFLength","MIFLength","Slot","Antenna","RoleAccepted","DescriptorSubmitted"};
 const uint64_t values[]={1,awdlNativeSearch.epoch,awdlNativeSearch.generation,reason,radio,channel,sampled,frames,psfLength,mifLength,context.slot,context.antenna,0,0};
 bool ok=true;for(unsigned n=0;n<14&&ok;++n){auto v=OSNumber::withNumber(values[n],64);if(!v){ok=false;break;}ok=d->setObject(keys[n],v);v->release();}
 if(ok)root->setProperty("AWDLNativeSyncStatePreflight",d);d->release();
}
