// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "DataDriver.hpp"
#include "LocalDataIdentity.hpp"
#include "NativeQueueRuntime.hpp"
bool ItlIwm::awdlIbssLocalAddress(uint8_t out[6]){
 if(!getMainWorkLoop()||!getMainWorkLoop()->inGate()){if(out)memset(out,0,6);return false;}
 if(awdlNativeSearchAddress(out))return true;
 const bool native=itlIbssDataOwner==this&&itlIbssDataTrial.nativeQueueMode;
 return ItlAwdl::selectLocalDataIdentity(native,com.sc_ic.ic_myaddr,ItlAwdl::gDataPort.registeredLocal,
                                       ItlAwdl::gDataPort.owner!=nullptr,out);
}
bool ItlIwm::awdlIbssNativeStartIdle(){
 if(!getMainWorkLoop()||!getMainWorkLoop()->inGate())return false;
 const auto &port=ItlAwdl::gDataPort;
 return port.owner&&!port.releasePending&&!port.nativeSession&&!port.rxCount&&!port.tx.enabled&&
  port.tx.empty()&&port.queueActivation.idle()&&!port.hardware.pending&&
  !port.linkBusy&&!port.linkUp&&!port.linkDownPending;
}
bool ItlIwm::awdlIbssNativePending(){
 if(!getMainWorkLoop()||!getMainWorkLoop()->inGate())return false;
 const auto &t=itlIbssDataTrial;
 return itlIbssDataOwner==this&&t.nativeQueueMode&&t.active()&&
  t.phase!=ItlIbss::DataTrialPhase::Recovering&&!t.cancelled&&!t.endRequested;
}
void ItlIwm::awdlIbssNativeQuiesce(){
 if(!getMainWorkLoop()||!getMainWorkLoop()->inGate()||itlIbssDataOwner!=this||!itlIbssDataTrial.nativeQueueMode)return;
 auto &t=itlIbssDataTrial;t.endRequested=true;
 if(t.nativeDeadlineReached&&!t.cancelled&&!t.nativeError&&t.nativeStarted)t.result=ItlIbss::DataTrialResult::NativeConfigured;
 else if(t.nativeError)t.result=ItlIbss::DataTrialResult::PrepareFailed;
 else{t.cancelled=true;t.result=ItlIbss::DataTrialResult::Cancelled;}
 getMainCommandGate()->commandWakeup(&awdlIbssWorkPending,false);
}
IOReturn ItlIwm::awdlIbssNativeRequest(const ItlAwdl::SingleDataRequest &input,const uint8_t current[6]){
 auto &port=ItlAwdl::gDataPort;
 if(!awdlIbssNativeStartIdle()||!current||memcmp(current,port.registeredLocal,6))return kIOReturnNotReady;
 uint8_t frame[104];size_t n=0;
 if(!ItlAwdl::buildSingleDataEcho(input,current,uint32_t(com.sc_generation),1,frame,sizeof(frame),n))return kIOReturnBadArgument;
 const auto result=awdlIbssDataRequest(input,false,3,true);if(result!=kIOReturnSuccess)return result;
 // Commit identity and mode in the same gate, before the retained worker runs.
 // The previous stopped session keeps this marker until a new request commits.
 port.nativeStopPending=false;
 port.nativeAutoPeer=false;port.nativePeerGeneration=port.nativePeerEpoch=0;
 memset(port.nativePeerMAC,0,6);memset(port.nativePeerIP,0,16);
 auto &t=itlIbssDataTrial;t.nativeQueueMode=true;memcpy(t.local,current,6);
 memcpy(t.request.sourceIp,input.sourceIp,16);memcpy(port.nativeSource,input.sourceIp,16);
 awdlIbssDataPublish();return kIOReturnSuccess;
}
void ItlIwm::awdlIbssNativeWake(){
 if(getMainWorkLoop()&&getMainWorkLoop()->inGate()&&itlNativeSearchPeerOwned(this)){
  getMainCommandGate()->commandWakeup(&awdlIbssWorkPending,false);return;
 }
 if(getMainWorkLoop()&&getMainWorkLoop()->inGate()&&itlIbssDataOwner==this&&
    itlIbssDataTrial.nativeQueueMode&&awdlIbssWorkPending)
  getMainCommandGate()->commandWakeup(&awdlIbssWorkPending,false);
}
// Included after the peer and trial runtimes; activation requires the explicit native request.
uint32_t ItlIwm::awdlIbssNativeLeaseReason(){
 auto p=itlIbssPeerPort;auto &t=itlIbssDataTrial;auto &s=awdlIbssExclusive;auto &port=ItlAwdl::gDataPort;
 if(!getMainWorkLoop()||!getMainWorkLoop()->inGate())return 1;
 if(itlIbssDataOwner!=this||!t.nativeQueueMode)return 2;
 if(t.cancelled||t.endRequested)return 3;
 if(dmaDetaching||s.resetRequested)return 4;
 if(!awdlProbeCounters||!p||p->hal!=this||p->retiring)return 5;
 if(!port.owner||!port.nativeSession||!port.tx.enabled)return 6;
 if(memcmp(port.registeredLocal,p->reserved.local,6)||memcmp(port.tx.local,port.registeredLocal,6))return 7;
 const auto gen=uint32_t(com.sc_generation);
 if(!gen||p->reserved.generation!=gen||port.tx.generation!=gen)return 8;
 if(!s.ownsRadio||s.phase!=ItlIbss::ExclusivePhase::FirmwareReady||s.operation)return 9;
 if(!p->peer.ready||p->peer.unknown||p->peer.released)return 10;
 const auto now=itlAwdlRocNowNs();if(now>=port.tx.deadlineNs||now>=s.deadline)return 11;
 if(!ItlAwdl::nativeDataQueuesReady(port.txQueue,port.rxQueue)||
    ItlAwdl::dataNativeWorkLoop(port.txQueue)!=getMainWorkLoop()||
    ItlAwdl::dataNativeWorkLoop(port.rxQueue)!=getMainWorkLoop())return 12;
 return 0;
}
int ItlIwm::awdlIbssNativeOfferBsd(const uint8_t *eth,size_t n){
 if(awdlIbssNativeLeaseReason())return ENETDOWN;
 auto &port=ItlAwdl::gDataPort;auto &t=itlIbssDataTrial;
 if(ItlAwdl::validateDataEthernet(eth,n,port.registeredLocal)!=ItlAwdl::DataError::Ok)return EINVAL;
 if(memcmp(eth,t.request.peer,6)||memcmp(eth+22,port.nativeSource,16)||memcmp(eth+38,t.request.peerIp,16))return EHOSTUNREACH;
 ItlAwdl::DataTicket ticket;
 if(!port.tx.pushCopied(eth,n,port.tx.generation,itlAwdlRocNowNs(),port.wifiSequence,port.awdlSequence,ticket))return ENOBUFS;
 port.wifiSequence=uint16_t((port.wifiSequence+1)&4095);++port.awdlSequence;
 awdlIbssNativeWake();return 0;
}
bool ItlIwm::awdlIbssNativeContext(ItlAwdl::DataContext &out,bool preparing){
 if(!preparing&&awdlIbssNativeLeaseReason())return false;
 auto p=itlIbssPeerPort;auto &t=itlIbssDataTrial;auto &s=awdlIbssExclusive;
 auto &port=ItlAwdl::gDataPort;
 if(itlIbssDataOwner!=this||!t.nativeQueueMode||t.cancelled||t.endRequested)return false;
 out={};out.gateHeld=getMainWorkLoop()&&getMainWorkLoop()->inGate();
 if(!out.gateHeld||dmaDetaching||!awdlProbeCounters||!p||p->hal!=this||p->retiring||
    !port.owner||(!preparing&&(!port.tx.enabled||!port.nativeSession))||
    memcmp(port.registeredLocal,p->reserved.local,6)||
    (port.tx.enabled&&memcmp(port.tx.local,port.registeredLocal,6))||
    !ItlAwdl::nativeDataQueuesReady(port.txQueue,port.rxQueue))return false;
 const auto before=itlAwdlRocNowNs();if(!iwm_nic_lock(&com))return false;
 out.gp2=iwm_read_prph(&com,IWM_DEVICE_SYSTEM_TIME_REG);iwm_nic_unlock(&com);
 out.nowNs=itlAwdlRocNowNs();if(out.nowNs<before||out.nowNs-before>2000000ULL)return false;
 out.generation=uint32_t(com.sc_generation);out.radioKind=ItlAwdl::DataRadioKind::ExclusiveIbss;
 out.channel=6;out.deadlineNs=preparing?s.deadline:(port.tx.deadlineNs<s.deadline?port.tx.deadlineNs:s.deadline);
 auto &lease=out.ibssStorage;lease.generation=p->reserved.generation;lease.deadlineNs=s.deadline;
 memcpy(lease.local,p->reserved.local,6);memcpy(lease.peer,p->reserved.peer,6);
 lease.ownsRadio=s.ownsRadio;lease.firmwareReady=s.phase==ItlIbss::ExclusivePhase::FirmwareReady&&!s.operation;
 lease.resetRequested=s.resetRequested;lease.peerReady=p->peer.ready&&!p->peer.unknown&&!p->peer.released;
 lease.peerFollowValid=itlIbssFixedDataAllowed(this,out.generation,out.gp2,out.nowNs);
 lease.window=&awdlProbeCounters->ibss.window;
 awdlProbeCounters->dataModelView(out);
 out.backendReady=port.nativeSession&&port.tx.enabled&&port.tx.generation==out.generation;
 return preparing?ItlAwdl::dataRadioContextReady(out):ItlAwdl::dataContextReady(out);
}
bool ItlIwm::awdlIbssNativeOpen(const uint8_t current[6]){
 if(!getMainWorkLoop()||!getMainWorkLoop()->inGate()||getMainWorkLoop()->onThread())return false;
 auto &port=ItlAwdl::gDataPort;auto &t=itlIbssDataTrial;
 if(itlIbssDataOwner!=this||!t.nativeQueueMode||t.cancelled||t.endRequested||!port.owner||
    !current||memcmp(current,port.registeredLocal,6)||port.tx.enabled||!port.tx.empty()||port.hardware.pending)return false;
 ItlAwdl::NativeQueueOps ops{getMainWorkLoop()};
 const auto fail=[&](){t.nativeError=EIO;port.stop(false,ECANCELED);awdlIbssNativeQuiesce();awdlIbssNativeQueuesStop();return false;};
 if(!port.queueActivation.start(ops,port.txQueue,port.rxQueue))return fail();
 ItlAwdl::DataContext context;if(!awdlIbssNativeContext(context,true))return fail();
 if(port.nativeAutoPeer){
  if(memcmp(port.nativePeerMAC,t.request.peer,6)||memcmp(port.nativePeerIP,t.request.peerIp,16)||!context.peers)return fail();
  const ItlAwdl::Peer *peer=nullptr;
  for(unsigned i=0;i<context.peers->count;++i)if(!memcmp(context.peers->peers[i].address,port.nativePeerMAC,6)){peer=&context.peers->peers[i];break;}
  if(!peer||!ItlAwdl::dataPeerAvailable(*peer,current,context,false))return fail();
  port.nativePeerGeneration=context.generation;port.nativePeerEpoch=context.modelEpoch;
 }
 const auto remaining=context.deadlineNs-context.nowNs;
 const auto end=context.nowNs+(remaining<2500000000ULL?remaining:2500000000ULL);
 if(!port.openNative(context,current,end)||!port.publishNativeLink(true))return fail();
 return true;
}
void ItlIwm::awdlIbssNativeQueuesStop(){
 auto &port=ItlAwdl::gDataPort;
 if(!getMainWorkLoop()||!getMainWorkLoop()->inGate()||port.tx.enabled||!port.tx.empty()||port.hardware.pending)return;
 ItlAwdl::NativeQueueOps ops{getMainWorkLoop()};const bool stopped=port.queueActivation.stop(ops);
 if(itlIbssDataOwner==this&&itlIbssDataTrial.nativeQueueMode)
  itlIbssDataTrial.nativeTeardownComplete=stopped&&!port.linkUp&&!port.linkBusy&&!port.linkDownPending;
}
static bool itlIbssNativeComplete(ItlIwm *hal,uint32_t gen,unsigned queue,unsigned index,uint32_t raw){
 if(itlNativeSearchPeerOwned(hal))return itlNativeSearchPeerComplete(hal,gen,queue,index,raw);
 if(itlIbssDataOwner!=hal||!itlIbssDataTrial.nativeQueueMode)return true;
 auto &port=ItlAwdl::gDataPort;int diagnostic=0;
 const int status=(raw&IWM_TX_STATUS_MSK)==IWM_TX_STATUS_SUCCESS?0:EIO;
 const bool done=port.finishHardware(gen,queue,index,status,diagnostic);
 if(done)++itlIbssDataTrial.nativeFrameCompleted;
 // ACK retires this descriptor; the next frame revalidates the still-live window.
 if(done&&!status&&!diagnostic&&hal->awdlProbeCounters){port.txNotifyValid=false;hal->awdlIbssNativeWake();}
 if(status||diagnostic)port.stop(false,status?status:diagnostic);
 if(status||diagnostic)itlIbssDataTrial.nativeError=uint32_t(status?status:diagnostic);
 if(!port.tx.enabled)hal->awdlIbssNativeQueuesStop();
 return done;
}
static bool itlIbssNativeMayTransmit(ItlIwm *hal,const ItlIbss::PeerGrant &grant){
 auto p=itlIbssPeerPort;auto &port=ItlAwdl::gDataPort;
 if(!p||p->hal!=hal||!ItlIbss::samePeerGrant(grant,p->reserved)||!port.hardware.pending||
    port.hardware.queue!=grant.queue||port.hardware.ticket.generation!=grant.generation||
    port.hardware.index!=unsigned(hal->com.txq[grant.queue].cur))return false;
 ItlAwdl::DataContext context;
 return hal->awdlIbssNativeContext(context)&&ItlAwdl::dataDestinationAllowed(grant.peer,grant.local,context);
}
static bool itlIbssNativeRetainPeer(ItlIwm *hal,uint32_t gen,uint64_t now){
 return itlIbssDataOwner==hal&&itlIbssDataTrial.nativeQueueMode&&ItlAwdl::gDataPort.tx.enabled&&
  ItlAwdl::gDataPort.tx.generation==gen&&now<ItlAwdl::gDataPort.tx.deadlineNs;
}
static void itlIbssNativeStop(ItlIwm *hal,bool physicalStopProven){
 if(itlIbssDataOwner==hal&&itlIbssDataTrial.nativeQueueMode){
  ItlAwdl::gDataPort.stop(physicalStopProven,ECANCELED);
  hal->awdlIbssNativeQueuesStop();
 }
}
static void itlIbssNativeTick(ItlIwm *hal){
 if(itlIbssDataOwner!=hal||!itlIbssDataTrial.nativeQueueMode)return;
 ++itlIbssDataTrial.nativeTxTicks;
 auto p=itlIbssPeerPort;auto &port=ItlAwdl::gDataPort;
 if(port.nativeStopPending){
  if(!itlIbssDataTrial.nativeStopNs)itlIbssDataTrial.nativeStopNs=itlAwdlRocNowNs();
  if(itlIbssDataTrial.nativeStarted&&!itlIbssDataTrial.cancelled&&!itlIbssDataTrial.nativeError&&itlAwdlRocNowNs()>=port.tx.deadlineNs)
   itlIbssDataTrial.nativeDeadlineReached=true;
  if(!itlIbssDataTrial.nativeLeaseReject)itlIbssDataTrial.nativeLeaseReject=itlIbssDataTrial.nativeDeadlineReached?11:13;
  port.publishNativeLink(false);hal->awdlIbssNativeQuiesce();hal->awdlIbssNativeQueuesStop();return;
 }
 if(!port.nativeSession)return; // Radio preparation is not yet a committed data session.
 const auto stop=[&](uint32_t reason){
  auto &t=itlIbssDataTrial;t.nativeLeaseReject=reason;t.nativeStopNs=itlAwdlRocNowNs();
  if(reason==11&&t.nativeStarted&&!t.cancelled&&!t.nativeError&&t.nativeStopNs>=port.tx.deadlineNs)
   t.nativeDeadlineReached=true;
  else if(reason!=3&&!t.cancelled&&!t.endRequested)t.nativeError=EIO;
  port.stop(false,ECANCELED);hal->awdlIbssNativeQuiesce();
 };
 const auto reason=hal->awdlIbssNativeLeaseReason();if(reason){stop(reason);return;}
 if(!p->onWorker())return;
 ItlAwdl::DataContext context;
 if(!hal->awdlIbssNativeContext(context)){
  const auto changed=hal->awdlIbssNativeLeaseReason();if(changed){stop(changed);return;}
  // A busy clock read or not-yet-refreshed peer proof denies all data I/O,
  // but must leave residency alive so the control scheduler can refresh it.
  auto &t=itlIbssDataTrial;if(t.nativeDeferrals<UINT32_MAX)++t.nativeDeferrals;
  if(!t.nativeFirstDeferredNs)t.nativeFirstDeferredNs=itlAwdlRocNowNs();
  return;
 }
 if(port.rxCount){if(port.bsdInputAction)port.deliverBsd(context);else port.notifyReceive();}
 bool queued=false;for(const auto &s:port.tx.slots)queued|=s.phase==ItlAwdl::DataPhase::Queued;
 if(queued)++itlIbssDataTrial.nativeQueuedTicks;
 if(p->data.pending()||port.hardware.pending)return;
 if(!ItlAwdl::dataActualWindow(context,true)){if(queued)++itlIbssDataTrial.nativeWindowDenied;return;}
 if(!queued){
  const auto window=context.ibssView()->window;
  if(window&&(!port.txNotifyValid||port.txNotifyWindow!=window->plan.open)&&port.notifyTransmit()==0){
   port.txNotifyWindow=window->plan.open;port.txNotifyValid=true;
  }
  return;
 }
 // Initial system experiment retains the proven three-packet transport cap.
 if(p->data.completed()&&!p->data.rearmAfterAck()){port.stop(false,ECANCELED);return;}
 const unsigned index=unsigned(hal->com.txq[p->reserved.queue].cur);
 if(!p->queueDrained(p->reserved))return;
 ItlAwdl::DataTransfer frame;
 if(!port.takeForHardware(context,p->reserved.queue,index,frame))return;
 ++itlIbssDataTrial.nativeTaken;
 ++p->calls;const bool sent=p->data.start(frame.bytes,frame.length);
 if(sent)++itlIbssDataTrial.nativeFrameStarted;
 if(!sent){
  itlIbssDataTrial.nativeError=EIO;
  int diagnostic=0;
  if(!p->data.publishedToHardware())port.abortBeforeHardwarePublication(true,EIO,diagnostic);
  port.stop(false,EIO);p->fault();
 }
 --p->calls;
}
static void itlIbssNativeReceive(ItlIwm *hal,const uint8_t *wire,size_t length){
 if(itlIbssDataOwner!=hal||!itlIbssDataTrial.nativeQueueMode)return;
 ItlAwdl::DataContext context;
 auto &port=ItlAwdl::gDataPort;
 if(hal->awdlIbssNativeContext(context)&&port.receive(wire,length,context)){
  if(port.bsdInputAction)port.deliverBsd(context);else port.notifyReceive();
 }
}
