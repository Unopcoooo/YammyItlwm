// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "PeerTable.hpp"
namespace ItlAwdl {
// Current channel-six visit on a fixed radio. Use the current EAW, not
// the next one returned by the prediction API. All clock quantities are us.
struct PeerMulticastWindow {
 bool hasSchedule=false;
 uint32_t peer=0,remaining=0,slot=0;
 void consider(const Peer &p,uint32_t gp2){
  if(!p.diagnosticId || !p.hasSync || !p.hasChannels || !p.timing.consistent ||
     p.timing.streak<2 || p.sync.awPeriod!=16 || p.sync.presenceMode!=4 ||
     !scheduleConsistentWithSync(p.sync,p.channels))return;
  WindowPrediction w;
  if(!predictWindowWithTxDelay(p.sync,p.syncReceivedGp2,gp2,500000,p.timing.txDelayUs,w))return;
  bool visits=false;
  for(unsigned i=0;i<p.channels.count;++i)visits|=p.channels.entries[i].channel==6;
  if(!visits)return;
  hasSchedule=true;
  w.nextBoundarySequence=uint16_t(w.nextBoundarySequence-p.sync.presenceMode);
  SlotView current;
  if(!predictNextSlot(p.sync,p.channels,w,current)||current.channel!=6)return;
  constexpr uint32_t guard=16*1024+2048,period=65536;
  if(w.waitMicroseconds>period-guard || w.waitMicroseconds<=guard+2000)return;
  auto left=w.waitMicroseconds-guard;
  const uint32_t freshness=500000-uint32_t(gp2-p.syncReceivedGp2);
  if(freshness<left)left=freshness;
  if(left<=2000)return;
  if(left>remaining){peer=p.diagnosticId;slot=current.slot;remaining=left;}
 }
};
}
