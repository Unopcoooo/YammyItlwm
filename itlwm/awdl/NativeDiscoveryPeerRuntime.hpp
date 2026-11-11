// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "PeerMulticastWindow.hpp"
// Reuse the ECF3 station/queue and DMA owner, under the existing search worker.
static bool itlNativeSearchPortOwned(ItlIwm *h,ItlIbssPeerPort *p){
 const auto &s=h->awdlNativeDiscovery;if(!p||p->hal!=h)return false;
 const int i=s.peerIndex(p->reserved.peer);if(i<0)return false;const auto &peer=s.peers[i];
 return s.used&&(peer.preparing||peer.prepared||s.peerFlight)&&s.epoch==h->awdlNativeSearch.epoch&&
  p->reserved.generation==s.generation&&!memcmp(p->reserved.local,s.local,6);
}
static bool itlNativeSearchPeerOwned(ItlIwm *h){
 for(auto p:itlDiscoveryPeerPorts)if(itlNativeSearchPortOwned(h,p))return true;return false;
}
static bool itlNativeSearchPeerWindow(ItlIwm *h,const ItlAwdl::NativePeerKey &key,uint64_t &close,unsigned *reason=nullptr){
 if(reason)*reason=1;
 auto &s=h->awdlNativeDiscovery;
 if(!s.open||s.closed||!h->awdlNativeSearchEnabled()||!h->awdlProbeCounters||!key.valid())return false;
 const auto before=itlAwdlRocNowNs();if(!h->iwm_nic_lock(&h->com))return false;
 const auto gp2=h->iwm_read_prph(&h->com,IWM_DEVICE_SYSTEM_TIME_REG);h->iwm_nic_unlock(&h->com);
 const auto now=itlAwdlRocNowNs();if(now<before||now-before>2000000ULL||now>=s.endNs)return false;
 ItlAwdl::DataContext model;h->awdlProbeCounters->dataModelView(model);
 if(model.modelGeneration!=s.generation||model.modelEpoch!=key.epoch||!model.peers||
    now<model.modelHostNs||now-model.modelHostNs>500000000ULL)return false;
 ItlAwdl::PeerMulticastWindow w;
 for(unsigned i=0;i<model.peers->count;++i)
  if(!memcmp(model.peers->peers[i].address,key.mac,6))w.consider(model.peers->peers[i],gp2);
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
 auto &s=h->awdlNativeDiscovery;auto p=itlDiscoveryPeerPort(h,grant.peer);uint64_t close=0;
 const int i=s.peerIndex(grant.peer);if(i<0||!p)return false;auto &peer=s.peers[i];p->nativeWindowReason=0;
 return itlNativeSearchPortOwned(h,p)&&!p->retiring&&peer.prepared&&peer.published&&s.peerFlight&&s.flightPeer==unsigned(i)&&
  ItlIbss::samePeerGrant(grant,p->reserved)&&!h->awdlIbssTxPort&&!h->awdlNativeControlTx.schedule.pending&&
  itlNativeSearchPeerWindow(h,peer.key,close,&p->nativeWindowReason)&&itlAwdlRocNowNs()+2000000ULL<s.peerWindowEnd;
}
static bool itlNativeSearchPeerPrepare(ItlIwm *h){
 auto &s=h->awdlNativeDiscovery;
 if(!s.open||s.closed||s.peerFlight||s.peerPreparing||h->awdlIbssTxPort||h->awdlNativeControlTx.schedule.pending)return true;
 if(h->awdlProbeCounters){
  ItlAwdl::DataContext model;h->awdlProbeCounters->dataModelView(model);
  const auto now=itlAwdlRocNowNs();
  uint32_t gp2=0;
  if(model.peers&&model.peers->count&&h->iwm_nic_lock(&h->com)){
   gp2=h->iwm_read_prph(&h->com,IWM_DEVICE_SYSTEM_TIME_REG);
   h->iwm_nic_unlock(&h->com);
  }
  if(gp2&&model.peers&&model.modelEpoch&&model.modelGeneration==s.generation&&
     now>=model.modelHostNs&&now-model.modelHostNs<=500000000ULL)
   for(unsigned i=0;i<model.peers->count;++i)
    s.learnPeer(model.peers->peers[i],s.generation,model.modelEpoch,
     gp2);
 }
 for(unsigned i=0;i<s.PeerCapacity;++i){auto &peer=s.peers[i];if(!peer.key.valid()||peer.prepared)continue;
  const ItlAwdl::NativeDiscoverySession::Packet *packet=nullptr;
  for(unsigned j=0;j<s.rxCount;++j){auto &q=s.rx[(s.rxHead+j)%s.RxCapacity];if(!(q.bytes[0]&1)&&!memcmp(q.bytes+6,peer.key.mac,6)){packet=&q;break;}}
  uint8_t eth[ItlAwdl::kEtherDataMax],frame[ItlAwdl::kAwdlDataMax];size_t length=0;
  if(packet){
   memcpy(eth,packet->bytes,packet->length);memcpy(eth,peer.key.mac,6);memcpy(eth+6,s.local,6);
   memcpy(eth+22,packet->bytes+38,16);memcpy(eth+38,peer.key.ip,16);
   if(ItlAwdl::encodeData(eth,packet->length,s.local,s.wifiSequence,s.awdlSequence,frame,sizeof(frame),length)!=ItlAwdl::DataError::Ok)return false;
  }else if(!ItlAwdl::nativePeerProbeFrame(peer.key,s.local,s.wifiSequence,s.awdlSequence,frame,sizeof(frame),length))continue;
  peer.preparing=true;s.updatePeers();const bool prepared=h->awdlIbssPeerPrepare(frame,length);
  auto p=itlDiscoveryPeerPort(h,peer.key.mac);
  peer.prepared=prepared&&itlNativeSearchPortOwned(h,p)&&h->awdlNativeDiscoveryReady()&&s.open&&!s.closed;
  peer.preparing=false;s.updatePeers();
  if(!peer.prepared){++s.peerFailures;s.close();h->awdlNativeSearch.fail(s.epoch,EIO);}
  h->awdlNativeDiscoveryPublish();return peer.prepared;
 }
 return true;
}
static bool itlNativeSearchPeerSend(ItlIwm *h,const uint8_t *frame,size_t length,uint8_t id,uint64_t now,uint64_t close,uint8_t retries){
 auto &s=h->awdlNativeDiscovery;auto p=itlDiscoveryPeerPort(h,frame+4);
 const int i=s.peerIndex(frame+4);
 if(i<0||!p)return false;auto &peer=s.peers[i];
 if(!itlNativeSearchPortOwned(h,p)||!peer.prepared||!peer.published||s.peerFlight||
    !s.open||s.closed||h->awdlIbssTxPort||p->calls||p->retiring||
    !p->queueDrained(p->reserved)||length>sizeof(p->request)||
    (p->data.completed()&&!p->data.rearmWithinLease())){
  s.state(id,s.PrepareFailed);++s.peerFailures;s.close();h->awdlNativeSearch.fail(s.epoch,EIO);return false;
 }
 memcpy(p->request,frame,length);p->requestLength=length;p->softwareRetries=retries;s.peerWindowEnd=close;
 s.peerFlight=true;s.flightPeer=unsigned(i);s.peerRecord=id;
 const bool sent=h->awdlIbssPeerSubmit();
 if(!sent){
  const uint64_t current=itlAwdlRocNowNs();
  const bool elapsed=s.peerWindowEnd>=2000000ULL&&current>=s.peerWindowEnd-2000000ULL;
  const bool healthy=itlNativeSearchPortOwned(h,p)&&s.open&&!s.closed&&peer.prepared&&peer.published&&
   h->awdlNativeSearchEnabled()&&!h->awdlIbssExclusive.resetRequested&&!p->retiring&&!p->mismatch&&!p->held&&
   !h->awdlIbssTxPort&&!h->awdlNativeControlTx.schedule.pending&&p->queueDrained(p->reserved);
  // Keep actual window expiry separate from the other causes hidden by permission rejection 28.
  h->awdlIbssExperimentEvent(60,p->data.rejectionCode(),int(p->rejection),
   (elapsed?1u:0u)|(healthy?2u:0u),unsigned(length));
  h->awdlIbssExperimentEvent(64,p->nativeWindowReason,int(p->rejection),healthy?1u:0u,unsigned(length));
  // Only a fresh channel-window miss or local guard rejection can be deferred.
  const bool temporary=p->nativeWindowReason==2||p->nativeWindowReason==3;
  if(p->rejection==28&&(elapsed||temporary)&&healthy&&p->data.discardUnpublishedWindowMiss()){
   const bool queued=s.retryFront(p->request,p->requestLength,retries,id,current);
   s.peerFlight=false;s.peerRecord=0;
   if(!queued){s.state(id,s.WindowMiss);++s.rejected;}
   h->awdlNativeDiscoveryPublish();return false;
  }
 }
 if(!sent){s.state(id,s.TransportFailed);++s.peerFailures;s.close();h->awdlNativeSearch.fail(s.epoch,EIO);return false;}
 if(s.peerSubmitted<16)s.peerTxResults[s.peerSubmitted]=uint64_t(length)<<32;
 ++s.peerSubmitted;++s.submitted;s.state(id,s.Submitted);
 s.wifiSequence=uint16_t((s.wifiSequence+1)&4095);s.awdlSequence=uint16_t((s.awdlSequence+1)&4095);return true;
}
static bool itlNativeSearchPeerComplete(ItlIwm *h,uint32_t gen,unsigned queue,unsigned index,uint32_t raw){
 auto &s=h->awdlNativeDiscovery;auto p=itlDiscoveryPeerQueue(h,queue);
 if(!itlNativeSearchPortOwned(h,p)||!s.peerFlight||s.flightPeer>=s.PeerCapacity||
    memcmp(s.peers[s.flightPeer].key.mac,p->reserved.peer,6)||gen!=s.generation)return false;
 // ItlIbssPeerPort already checked the immutable slot, SSN and completion owner.
 if(s.peerSubmitted&&s.peerSubmitted<=16)s.peerTxResults[s.peerSubmitted-1]|=
  uint64_t(raw)|(uint64_t(index)<<48)|(uint64_t(1)<<63);
 s.peerFlight=false;
 const bool ack=(raw&IWM_TX_STATUS_MSK)==IWM_TX_STATUS_SUCCESS;
 const bool lost=!ack&&p->data.retryExhaustedReclaimed()&&s.open&&!s.closed&&h->awdlNativeSearchEnabled()&&
  !h->awdlIbssExclusive.resetRequested&&!p->retiring&&!p->mismatch&&!p->held&&p->queueDrained(p->reserved);
 const uint8_t record=s.peerRecord;s.peerRecord=0;
 if(ack){++s.peerCompleted;++s.completed;}
 else if(lost){
  const bool reused=p->data.rearmAfterLoss();
  if(!reused||!s.retryFront(p->request,p->requestLength,p->softwareRetries,record,itlAwdlRocNowNs())){
   ++s.peerRetryExhausted;s.state(record,s.RetryExhausted);
  }
 }
 else{++s.peerFailures;s.close();h->awdlNativeSearch.fail(s.epoch,EIO);}
 if(ack)s.state(record,s.Drained);else if(!lost)s.state(record,s.TransportFailed);
 h->awdlIbssNativeWake();h->awdlNativeDiscoveryPublish();return true;
}
static void itlNativeSearchPeerStopped(ItlIwm *h,ItlIbssPeerPort *p){
 if(!itlNativeSearchPortOwned(h,p))return;
 if(!p->stopProof(p->reserved)||p->held||p->data.pending())return;
 auto &s=h->awdlNativeDiscovery;
 const int i=s.peerIndex(p->reserved.peer);if(i<0)return;
 if(s.peerFlight&&s.flightPeer==unsigned(i)){++s.peerFailures;s.state(s.peerRecord,s.TransportFailed);s.peerRecord=0;s.peerFlight=false;}
 s.peers[i].preparing=s.peers[i].prepared=false;s.updatePeers();
 h->awdlNativeDiscoveryPublish();
 // BSD peer withdrawal belongs to the retained control callout, not this IRQ.
}
