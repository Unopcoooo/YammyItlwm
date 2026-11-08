// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "NativeReceivePolicy.hpp"
#include <stdint.h>
#include <string.h>
namespace ItlAwdl {
// All access must hold the HAL gate; this is not a lock or a hardware proof.
// Metadata only. The gated IWM adapter must reserve a retained worker before
// commit and separately prove DMA retirement and ordinary-network recovery.
struct NativeSearchOwner {
 enum class Phase:uint32_t {Idle,Starting,Searching,Stopping,Recovering,Complete,Cancelled,Failed};
 Phase phase=Phase::Idle;
 uint64_t epoch=0,startNs=0,deadlineNs=0;
 uint32_t generation=0;
 uint8_t local[6]={};
 bool stopRequested=false,expired=false,configuredOnce=false;
 int32_t failure=0;
 bool receiveValidation()const{return deadlineNs>startNs&&deadlineNs-startNs>nativeReceiveLimit(false)&&deadlineNs-startNs<=nativeReceiveLimit(true);}
 bool active()const{return phase>=Phase::Starting&&phase<=Phase::Recovering;}
 bool reusable()const{return phase==Phase::Idle||phase==Phase::Complete||phase==Phase::Cancelled;}
 bool matches(uint64_t e)const{return e&&e==epoch;}
 bool commit(uint64_t e,const uint8_t address[6],uint64_t now,uint64_t duration,bool receiveValidation=false){
  if(!reusable()||!e||e<=epoch||!address||(address[0]&1)||!now||!duration||
     duration>nativeReceiveLimit(receiveValidation)||now>UINT64_MAX-duration)return false;
  uint8_t any=0;for(unsigned i=0;i<6;++i)any|=address[i];if(!any)return false;
  NativeSearchOwner next;next.phase=Phase::Starting;next.epoch=e;next.startNs=now;next.deadlineNs=now+duration;
  memcpy(next.local,address,6);*this=next;return true;
 }
 bool bindGeneration(uint64_t e,uint32_t g){
  if(!matches(e)||phase!=Phase::Starting||generation||!g||stopRequested)return false;
  generation=g;return true;
 }
 // Begin may sleep with the gate released; a Stop can arrive before it returns.
 bool recordEntered(uint64_t e,uint32_t g){
  if(!matches(e)||!g||(phase!=Phase::Starting&&phase!=Phase::Stopping))return false;
  if(generation)return generation==g;
  generation=g;return true;
 }
 bool configured(uint64_t e,uint32_t g,uint64_t now){
  if(!matches(e)||phase!=Phase::Starting||!generation||g!=generation||
     stopRequested||now<startNs||now>=deadlineNs)return false;
  configuredOnce=true;phase=Phase::Searching;return true;
 }
 bool stop(uint64_t e){
  if(!matches(e))return false;
  if(phase==Phase::Stopping||phase==Phase::Recovering){stopRequested=true;return true;}
  if(phase==Phase::Cancelled)return true;
  if(phase!=Phase::Starting&&phase!=Phase::Searching)return false;
  stopRequested=true;phase=Phase::Stopping;return true;
 }
 bool fail(uint64_t e,int32_t error){
  if(!matches(e)||!error||!active())return false;
  if(!failure)failure=error;
  if(phase!=Phase::Recovering)phase=Phase::Stopping;
  return true;
 }
 bool expire(uint64_t e,uint64_t now){
  if(!matches(e)||(phase!=Phase::Starting&&phase!=Phase::Searching)||now<deadlineNs)return false;
  expired=true;phase=Phase::Stopping;return true;
 }
 // No radio was entered: adapter must prove no submitted command or in-flight
 // worker operation, rather than converting a failed stop into this path.
 bool finishUnstarted(uint64_t e,bool pristine){
  if(!matches(e)||phase!=Phase::Stopping||generation||!pristine)return false;
  phase=!failure&&stopRequested?Phase::Cancelled:Phase::Failed;return true;
 }
 bool restoring(uint64_t e,uint32_t g){
  if(!matches(e)||phase!=Phase::Stopping||!generation||g!=generation)return false;
  phase=Phase::Recovering;return true;
 }
 // Generation checks concern the retired search image. Recovery may use new
 // STA generations; only the existing IWM observer can establish its proof.
 bool finish(uint64_t e,bool retired,bool stable,bool clean){
  if(!matches(e)||phase!=Phase::Recovering||!retired)return false;
  phase=stable&&clean&&!failure&&(stopRequested||configuredOnce)?(stopRequested?Phase::Cancelled:Phase::Complete):Phase::Failed;
  return true;
 }
 // Searching is receive residency, never a claim that BSD/peer data is ready.
};
}
