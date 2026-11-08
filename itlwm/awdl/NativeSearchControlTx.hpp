// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include "BoundedControlSchedule.hpp"
struct ItlIbssTxPort;
namespace ItlAwdl {
// One explicit pair or periodic diagnostic per HAL lifetime; never a role-ready state.
struct NativeSearchControlTx {
 enum Phase:uint32_t {Idle,Armed,Pending,Complete,Failed,Cancelled};
 Phase phase=Idle;uint32_t reason=0,generation=0,submitted=0,completed=0;
 uint64_t epoch=0,leaseEndNs=0,radioLeaseEndNs=0,epochHostNs=0,sampleHostNs=0;uint32_t epochGp2=0;
 ItlIbssTxPort *port=nullptr;
 bool periodic=false,nativeRoleAccepted=false,resumePriorMaster=false,drainedAtSearchStop=false;
 uint32_t stopBlockers=0;
 BoundedControlSchedule schedule;
 uint64_t roleQuietUntilNs=0;
 // Evidence from before stop/cancel can release any transport state.
 bool reusableTerminal()const{
  return phase==Complete || (phase==Cancelled && reason==1 && drainedAtSearchStop);
 }
 bool active()const{return phase==Armed||phase==Pending;}
};
}
