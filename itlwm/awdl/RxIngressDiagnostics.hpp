// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
namespace ItlIbss {
// Fixed counters only. Caller holds the main gate; no packet bytes or DMA ownership.
struct RxIngressDiagnostics {
 enum Event {BufferStart,DmaFailure,Notification,EmptyHeader,SentinelHeader,InvalidLength,BufferEnd};
 enum Scope {Capture,NoOwner,WrongGeneration,OutsideWindow};
 enum Metric {BuffersSeen,BuffersCaptured,ExcludedOwner,ExcludedGeneration,ExcludedWindow,
  DmaFailures,Notifications,MpduNotifications,EmptyHeaders,Sentinels,InvalidLengths,BufferEnds,Count};
 static constexpr unsigned MpduCode=0xc1;
 uint32_t total[Count]={},reply[Count]={};
 unsigned firstFault=0;uint32_t firstOffset=0,firstDetail=0;uint64_t firstNs=0;
 void add(Metric m,bool inReply){
  if(total[m]<UINT32_MAX)++total[m];
  if(inReply&&reply[m]<UINT32_MAX)++reply[m];
 }
 void begin(Scope scope,bool inReply){
  add(BuffersSeen,inReply);
  add(scope==Capture?BuffersCaptured:scope==NoOwner?ExcludedOwner:
      scope==WrongGeneration?ExcludedGeneration:ExcludedWindow,inReply);
 }
 void record(unsigned event,uint32_t offset,uint32_t detail,bool inReply,uint64_t now){
  switch(event){
   case DmaFailure:add(DmaFailures,inReply);break;
   case Notification:add(Notifications,inReply);if(detail==MpduCode)add(MpduNotifications,inReply);break;
   case EmptyHeader:add(EmptyHeaders,inReply);break;
   case SentinelHeader:add(Sentinels,inReply);break;
   case InvalidLength:add(InvalidLengths,inReply);break;
   case BufferEnd:add(BufferEnds,inReply);break;
   default:return;
  }
  // End markers are not evidence of a dropped frame.
  if(!firstFault&&(event==DmaFailure||event==InvalidLength)){
   firstFault=event;firstOffset=offset;firstDetail=detail;firstNs=now;
  }
 }
};
}
