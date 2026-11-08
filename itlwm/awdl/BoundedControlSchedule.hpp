// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "NativeReceivePolicy.hpp"
#include <stdint.h>
#include "NativeSyncProfile.hpp"
namespace ItlAwdl {
// A late worker sends at most one frame, never a catch-up burst. The radio
// lease and one outstanding descriptor remain the transport's authority.
struct BoundedControlSchedule {
 static constexpr uint64_t Tu=1024000ULL, Slot=NativeSyncProfile::awPeriod*NativeSyncProfile::presence*Tu, PsfPeriod=NativeSyncProfile::advertisementPeriod*Tu;
 enum Kind:uint8_t {None=255,Psf=0,Mif=3};
 uint32_t generation=0,submitted=0,completed=0,psf=0,mif=0;
 uint64_t endNs=0,nextPsf=0,nextMif=0,lastNs=0;
 bool armed=false,stopped=false,pending=false;Kind inFlight=None;
 uint32_t psfPeriodTu=NativeSyncProfile::advertisementPeriod;
 uint64_t lastPsfNs=0;
 bool setPsfPeriod(uint32_t gen,uint64_t now,uint32_t tu){
  if(!armed||stopped||gen!=generation||now<lastNs||now>=endNs||
     !NativeSyncProfile::supportsAdvertisementPeriod(tu))return false;
  if(tu==psfPeriodTu)return true;
  // Leave an in-flight descriptor untouched; the next frame uses the new cadence.
  psfPeriodTu=tu;lastNs=now;
  if(psf){const auto interval=uint64_t(tu)*Tu;
   nextPsf=interval>=endNs-lastPsfNs?endNs:lastPsfNs+interval;}
  return true;
 }
 bool arm(uint32_t gen,uint64_t now,uint64_t end,uint16_t down,bool receiveValidation=false){
  if(armed||!gen||!now||now>UINT64_MAX-nativeReceiveLimit(receiveValidation)||end<=now||end-now>nativeReceiveLimit(receiveValidation)||!down||down>64)return false;
  generation=gen;lastNs=now;endNs=end;nextPsf=now;
  nextMif=now+uint64_t(down>32?down-32:down+32)*Tu;armed=true;return true;
 }
 void stop(){stopped=true;}
 Kind due(uint32_t gen,uint64_t now,bool eligible){
  if(!armed||stopped)return None;
  if(gen!=generation||now<lastNs||now>=endNs){stop();return None;}
  lastNs=now;if(pending||!eligible)return None;
  if(now>=nextPsf && (now<nextMif||nextPsf<=nextMif))return Psf;
  return now>=nextMif?Mif:None;
 }
 bool publish(Kind kind,uint32_t gen,uint64_t now,uint16_t down){
  if((kind!=Psf&&kind!=Mif)||!down||down>64||due(gen,now,true)!=kind)return false;
  pending=true;inFlight=kind;++submitted;
  if(kind==Psf){
   ++psf;
   lastPsfNs=now;
   // Keep the selected phase after late wakes, without catch-up frames.
   const auto period=uint64_t(psfPeriodTu)*Tu;
   const auto advance=((now-nextPsf)/period+1)*period;
   nextPsf=advance>=endNs-nextPsf?endNs:nextPsf+advance;
  }
  else {++mif;nextMif=now+uint64_t(down>32?down-32:down+32)*Tu;}
  return true;
 }
 bool complete(bool success){
  if(!pending)return false;pending=false;inFlight=None;
  if(!success){stop();return false;}++completed;return true;
 }
 uint32_t wakeUs(uint64_t now)const{
  if(!armed||stopped)return 250000;
  if(pending)return 1000;
  const auto dueNs=nextPsf<nextMif?nextPsf:nextMif;
  if(dueNs<=now)return 1000;
  const auto us=(dueNs-now+999)/1000;return uint32_t(us>250000?250000:us);
 }
};
}
