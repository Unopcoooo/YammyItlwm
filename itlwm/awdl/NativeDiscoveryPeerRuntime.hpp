// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "PeerMulticastWindow.hpp"
// Reuse the ECF3 station/queue and DMA owner, under the existing search worker.
static bool itlNativeSearchPeerOwned(ItlIwm *h){
 const auto &s=h->awdlNativeDiscovery;auto p=itlIbssPeerPort;
 return s.used&&(s.peerPreparing||s.peerPrepared||s.peerFlight)&&p&&p->hal==h&&
  s.epoch==h->awdlNativeSearch.epoch&&s.peerKey.valid()&&
  p->reserved.generation==s.generation&&!memcmp(p->reserved.local,s.local,6)&&
  !memcmp(p->reserved.peer,s.peerKey.mac,6);
}
static bool itlNativeSearchPeerWindow(ItlIwm *h,uint64_t &close,unsigned *reason=nullptr){
 if(reason)*reason=1;
 auto &s=h->awdlNativeDiscovery;
 if(!s.open||s.closed||!h->awdlNativeSearchEnabled()||!h->awdlProbeCounters||!s.peerKey.valid())return false;
 const auto before=itlAwdlRocNowNs();if(!h->iwm_nic_lock(&h->com))return false;
 const auto gp2=h->iwm_read_prph(&h->com,IWM_DEVICE_SYSTEM_TIME_REG);h->iwm_nic_unlock(&h->com);
 const auto now=itlAwdlRocNowNs();if(now<before||now-before>2000000ULL||now>=s.endNs)return false;
 ItlAwdl::DataContext model;h->awdlProbeCounters->dataModelView(model);
 if(model.modelGeneration!=s.generation||model.modelEpoch!=s.peerKey.epoch||!model.peers||
    now<model.modelHostNs||now-model.modelHostNs>500000000ULL)return false;
 ItlAwdl::PeerMulticastWindow w;
 for(unsigned i=0;i<model.peers->count;++i)
  if(!memcmp(model.peers->peers[i].address,s.peerKey.mac,6))w.consider(model.peers->peers[i],gp2);
 const auto &t=h->awdlNativeControlTx;
 if(!t.epochHostNs||now<t.epochHostNs)return false;
 const uint64_t age=(now-t.epochHostNs)/1000;const uint32_t elapsed=gp2-t.epochGp2;
 if(age>ItlAwdl::nativeReceiveLimit(h->awdlNativeSearch.receiveValidation())/1000||elapsed>ItlAwdl::nativeReceiveLimit(h->awdlNativeSearch.receiveValidation())/1000||(age>elapsed?age-elapsed:elapsed-age)>2048)return false;
 if(!w.peer){if(reason)*reason=2;return false;}
 const uint32_t phase=elapsed%65536,guard=5120;
 if(phase<guard||phase+2000>=65536-guard){if(reason)*reason=3;return false;}
 uint32_t span=65536-guard-phase;if(w.remaining<span)span=w.remaining;
 const uint64_t ns=uint64_t(span)*1000;close=ns<s.endNs-now?now+ns:s.endNs;
 return close>now&&close-now>2000000ULL;
}
static bool itlNativeSearchPeerMayTransmit(ItlIwm *h,const ItlIbss::PeerGrant &grant){
 auto &s=h->awdlNativeDiscovery;auto p=itlIbssPeerPort;uint64_t close=0;
 if(p)p->nativeWindowReason=0;
 return itlNativeSearchPeerOwned(h)&&!p->retiring&&
  s.peerPrepared&&s.peerPublished&&s.peerFlight&&ItlIbss::samePeerGrant(grant,p->reserved)&&
  !h->awdlIbssTxPort&&!h->awdlNativeControlTx.schedule.pending&&
  itlNativeSearchPeerWindow(h,close,&p->nativeWindowReason)&&itlAwdlRocNowNs()+2000000ULL<s.peerWindowEnd;
}
static bool itlNativeSearchPeerPrepare(ItlIwm *h){
 auto &s=h->awdlNativeDiscovery;
 if(!s.peerKey.valid()||s.peerPrepared)return true;
 if(s.peerPreparing||!s.open||s.closed||!s.rxCount||h->awdlIbssTxPort||h->awdlNativeControlTx.schedule.pending)return false;
 // The first validated addressed packet chooses the peer; no invented peer/IP.
 const auto &packet=s.rx[s.rxHead];
 if(!s.peerPacket(packet.bytes,packet.length,false))return true;
 uint64_t close=0;if(!itlNativeSearchPeerWindow(h,close))return false;
 uint8_t eth[ItlAwdl::kEtherDataMax],frame[ItlAwdl::kAwdlDataMax];size_t length=0;
 memcpy(eth,packet.bytes,packet.length);memcpy(eth,s.peerKey.mac,6);memcpy(eth+6,s.local,6);
 memcpy(eth+22,packet.bytes+38,16);memcpy(eth+38,s.peerKey.ip,16);
 if(ItlAwdl::encodeData(eth,packet.length,s.local,s.wifiSequence,s.awdlSequence,frame,sizeof(frame),length)!=ItlAwdl::DataError::Ok)return false;
 s.peerPreparing=true;
 const bool prepared=h->awdlIbssPeerPrepare(frame,length);
 // Prepare may sleep. The generated frame is ONLY a station identity input;
 // it is never submitted. Stop/generation/owner changes invalidate admission.
 s.peerPrepared=prepared&&itlNativeSearchPeerOwned(h)&&h->awdlNativeDiscoveryReady()&&s.open&&!s.closed;
 s.peerPreparing=false;
 if(!s.peerPrepared){++s.peerFailures;s.close();h->awdlNativeSearch.fail(s.epoch,EIO);}
 h->awdlNativeDiscoveryPublish();return s.peerPrepared;
}
static bool itlNativeSearchPeerSend(ItlIwm *h,const uint8_t *frame,size_t length,uint8_t id,uint64_t now,uint64_t close){
 auto &s=h->awdlNativeDiscovery;auto p=itlIbssPeerPort;
 if(!itlNativeSearchPeerOwned(h)||!s.peerPrepared||!s.peerPublished||s.peerFlight||
    !s.open||s.closed||h->awdlIbssTxPort||p->calls||p->retiring||
    !p->queueDrained(p->reserved)||length>sizeof(p->request)||
    (p->data.completed()&&!p->data.rearmWithinLease())){
  s.state(id,s.PrepareFailed);++s.peerFailures;s.close();h->awdlNativeSearch.fail(s.epoch,EIO);return false;
 }
 memcpy(p->request,frame,length);p->requestLength=length;s.peerWindowEnd=close;
 s.peerFlight=true;s.peerRecord=id;
 const bool sent=h->awdlIbssPeerSubmit();
 if(!sent){
  const uint64_t current=itlAwdlRocNowNs();
  const bool elapsed=s.peerWindowEnd>=2000000ULL&&current>=s.peerWindowEnd-2000000ULL;
  const bool healthy=itlNativeSearchPeerOwned(h)&&s.open&&!s.closed&&s.peerPrepared&&s.peerPublished&&
   h->awdlNativeSearchEnabled()&&!h->awdlIbssExclusive.resetRequested&&!p->retiring&&!p->mismatch&&!p->held&&
   !h->awdlIbssTxPort&&!h->awdlNativeControlTx.schedule.pending&&p->queueDrained(p->reserved);
  // Keep actual window expiry separate from the other causes hidden by permission rejection 28.
  h->awdlIbssExperimentEvent(60,p->data.rejectionCode(),int(p->rejection),
   (elapsed?1u:0u)|(healthy?2u:0u),unsigned(length));
  h->awdlIbssExperimentEvent(64,p->nativeWindowReason,int(p->rejection),healthy?1u:0u,unsigned(length));
  // Only a fresh channel-window miss or local guard rejection can be deferred.
  const bool temporary=p->nativeWindowReason==2||p->nativeWindowReason==3;
  if(p->rejection==28&&(elapsed||temporary)&&healthy&&p->data.discardUnpublishedWindowMiss()){
   s.peerFlight=false;s.peerRecord=0;s.state(id,s.WindowMiss);++s.rejected;
   h->awdlNativeDiscoveryPublish();return false;
  }
 }
 if(!sent){s.state(id,s.TransportFailed);++s.peerFailures;s.close();h->awdlNativeSearch.fail(s.epoch,EIO);return false;}
 if(s.peerSubmitted<16)s.peerTxResults[s.peerSubmitted]=uint64_t(length)<<32;
 ++s.peerSubmitted;++s.submitted;s.state(id,s.Submitted);
 s.wifiSequence=uint16_t((s.wifiSequence+1)&4095);s.awdlSequence=uint16_t((s.awdlSequence+1)&4095);return true;
}
static bool itlNativeSearchPeerComplete(ItlIwm *h,uint32_t gen,unsigned queue,unsigned index,uint32_t raw){
 auto &s=h->awdlNativeDiscovery;auto p=itlIbssPeerPort;
 if(!itlNativeSearchPeerOwned(h)||!s.peerFlight||gen!=s.generation||queue!=p->reserved.queue)return false;
 // ItlIbssPeerPort already checked the immutable slot, SSN and completion owner.
 if(s.peerSubmitted&&s.peerSubmitted<=16)s.peerTxResults[s.peerSubmitted-1]|=
  uint64_t(raw)|(uint64_t(index)<<48)|(uint64_t(1)<<63);
 s.peerFlight=false;
 const bool ack=(raw&IWM_TX_STATUS_MSK)==IWM_TX_STATUS_SUCCESS;
 const bool lost=!ack&&p->data.retryExhaustedReclaimed()&&s.open&&!s.closed&&h->awdlNativeSearchEnabled()&&
  !h->awdlIbssExclusive.resetRequested&&!p->retiring&&!p->mismatch&&!p->held&&p->queueDrained(p->reserved);
 s.state(s.peerRecord,ack?s.Drained:lost?s.RetryExhausted:s.TransportFailed);s.peerRecord=0;
 if(ack){++s.peerCompleted;++s.completed;}
 else if(lost){++s.peerRetryExhausted;}
 else{++s.peerFailures;s.close();h->awdlNativeSearch.fail(s.epoch,EIO);}
 h->awdlIbssNativeWake();h->awdlNativeDiscoveryPublish();return true;
}
static void itlNativeSearchPeerStopped(ItlIwm *h){
 if(!itlNativeSearchPeerOwned(h))return;
 auto p=itlIbssPeerPort;if(!p->stopProof(p->reserved)||p->held||p->data.pending())return;
 auto &s=h->awdlNativeDiscovery;
 if(s.peerFlight){++s.peerFailures;s.state(s.peerRecord,s.TransportFailed);s.peerRecord=0;}
 s.peerFlight=false;s.peerPreparing=false;s.peerPrepared=false;
 h->awdlNativeDiscoveryPublish();
 // BSD peer withdrawal belongs to the retained control callout, not this IRQ.
}
