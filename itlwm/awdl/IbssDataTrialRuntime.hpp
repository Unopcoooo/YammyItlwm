// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "IbssDataTrial.hpp"
#include "FixedChannelControl.hpp"
static ItlIbss::DataTrial itlIbssDataTrial;
static ItlIwm *itlIbssDataOwner=nullptr;
static ItlAwdl::FixedChannelControl itlIbssFixedControl;
static ItlAwdl::FixedControlView itlIbssFixedAdvertised;
static ItlIwm *itlIbssFixedOwner=nullptr;
static uint32_t itlIbssFixedGeneration=0;
// Only the bounded trial's target feeds this prototype's control state.
static void itlIbssFixedObserve(ItlIwm *hal,const uint8_t *wire,size_t length,uint32_t gen,uint32_t gp2,uint64_t now){
 if(itlIbssDataOwner!=hal||(itlIbssDataTrial.passive()&&!itlIbssDataTrial.listenerTx)||length<24||memcmp(wire+10,itlIbssDataTrial.request.peer,6))return;
 if(itlIbssDataTrial.controlMode&2)itlIbssDataTrial.peerCapabilities.observe(wire,length,itlIbssDataTrial.request.peer,gen,now);
 if(itlIbssFixedOwner!=hal||itlIbssFixedGeneration!=gen){
  itlIbssFixedControl.reset(itlIbssDataTrial.local,gen);itlIbssFixedAdvertised={};
  itlIbssFixedOwner=hal;itlIbssFixedGeneration=gen;
 }
 itlIbssFixedControl.observe(wire,length,gen,gp2,now);
}
static bool itlIbssFixedDataAllowed(ItlIwm *hal,uint32_t gen,uint32_t gp2,uint64_t now){
 if(itlIbssFixedOwner!=hal||itlIbssFixedGeneration!=gen||itlIbssFixedAdvertised.generation!=gen)return false;
 ItlAwdl::FixedControlView v;if(!itlIbssFixedControl.view(gen,gp2,now,v))return false;
 const auto &a=v.election;const auto &b=itlIbssFixedAdvertised.election;
 return !memcmp(v.source,itlIbssDataTrial.request.peer,6)&&!memcmp(a.masterAddress,b.masterAddress,6)&&
  !memcmp(a.syncAddress,b.syncAddress,6)&&a.height==b.height&&a.masterMetric==b.masterMetric&&a.masterCounter==b.masterCounter;
}
static bool itlIbssPeerReplyWindowOpen(ItlIwm *hal,uint32_t generation,uint64_t now){
 if(itlNativeSearchPeerOwned(hal)){
  const auto &s=hal->awdlNativeDiscovery;
  return s.open&&!s.closed&&s.generation==generation&&now<s.endNs;
 }
 const auto &t=itlIbssDataTrial;
 if(t.nativeQueueMode)return itlIbssNativeRetainPeer(hal,generation,now);
 return itlIbssDataOwner==hal&&t.generation==generation&&
  (t.inReplyWindow(now)||(t.smallBatch.enabled&&t.smallBatch.endNs&&now<t.smallBatch.endNs&&
    !t.cancelled&&!t.endRequested&&t.phase>=ItlIbss::DataTrialPhase::WaitingWindow&&t.phase<=ItlIbss::DataTrialPhase::Submitted));
}
static void itlIbssApplyObservedCapabilities(ItlIwm *hal,ItlIbss::PeerGrant &g,unsigned streams){
 auto &t=itlIbssDataTrial;
 if(itlIbssDataOwner!=hal||!(t.controlMode&2)||t.passive()||t.cancelled||t.endRequested||
    memcmp(g.peer,t.request.peer,6)||!t.peerCapabilities.fresh(g.generation,itlAwdlRocNowNs()))return;
 g.capabilitiesObserved=true;g.capabilityFlags=t.peerCapabilities.stationFlags(streams);t.peerCapabilitiesApplied=1;
}
bool ItlIwm::awdlIbssDataActive(){return itlIbssDataOwner==this&&itlIbssDataTrial.active();}
void ItlIwm::awdlRxIngressEvent(unsigned event,uint32_t offset,uint32_t detail){
 // Never inspect trial state outside its owning gate, or allocate in the RX path.
 if(itlIbssDataOwner!=this||!awdlProbeCounters)return;
 auto loop=getMainWorkLoop();if(!loop||!loop->inGate())return;
 auto &t=itlIbssDataTrial;if(!t.active()||!t.generation)return;
 const auto now=itlAwdlRocNowNs();using D=ItlIbss::RxIngressDiagnostics;
 const auto scope=!awdlIbssExclusive.ownsRadio?D::NoOwner:
  uint32_t(com.sc_generation)!=t.generation?D::WrongGeneration:
  !awdlProbeCounters->ibss.captures(uint32_t(com.sc_generation),now)?D::OutsideWindow:D::Capture;
 const bool reply=t.inReplyWindow(now);
 if(event==D::BufferStart)t.ingress.begin(scope,reply);
 else if(scope==D::Capture)t.ingress.record(event,offset,detail,reply,now);
}
void ItlIwm::awdlIbssDataRxStage(unsigned stage,const ItlAwdl::RawRxMetadata &m){
 if(!awdlIbssDataActive()||!getMainWorkLoop()->inGate()||!awdlProbeCounters||!awdlIbssExclusive.ownsRadio)return;
 const auto now=itlAwdlRocNowNs();const auto gen=uint32_t(com.sc_generation);
 if(awdlProbeCounters->ibss.captures(gen,now))itlIbssDataTrial.observePeerRxStage(stage,m,gen,now);
}
bool ItlIwm::awdlListenerActive(){return awdlIbssDataActive()&&itlIbssDataTrial.listener;}
bool ItlIwm::awdlIbssReceiveOnlyActive(){return awdlIbssDataActive()&&itlIbssDataTrial.passiveIbss;}
bool ItlIwm::awdlListenerQueueReady(){
 if(!awdlListenerActive()||!itlIbssDataTrial.listenerQueueReserved||!getMainWorkLoop()->inGate()||
    !awdlIbssExclusive.ownsRadio||awdlIbssExclusive.resetRequested||dmaDetaching)return false;
 const auto &q=com.txq[IWM_DQA_INJECT_MONITOR_QUEUE];
 return q.desc&&q.cmd&&!q.queued&&q.cur==q.tail&&!(com.agg_queue_mask&(1u<<IWM_DQA_INJECT_MONITOR_QUEUE));
}
bool ItlIwm::awdlListenerReserveQueue(){
 if(!awdlListenerActive()||!getMainWorkLoop()->inGate()||getMainWorkLoop()->onThread()||
    !awdlIbssExclusive.ownsRadio||awdlIbssExclusive.resetRequested||dmaDetaching||
    awdlIbssExclusive.phase!=ItlIbss::ExclusivePhase::FirmwareReady||
    uint32_t(com.sc_generation)!=awdlIbssExclusive.session.generation||
    awdlIbssExclusive.session.prerequisitesConfirmed||itlIbssDataTrial.listenerQueueReserved)return false;
 if(memcmp(itlIbssDataTrial.local,com.sc_ic.ic_myaddr,6))return false;
 auto &q=com.txq[IWM_DQA_INJECT_MONITOR_QUEUE];
 if(!q.desc||!q.cmd||q.queued||q.cur!=q.tail||(com.agg_queue_mask&(1u<<IWM_DQA_INJECT_MONITOR_QUEUE)))return false;
 for(unsigned i=0;i<IWM_TX_RING_COUNT;++i)if(q.data[i].m||q.data[i].in)return false;
 // Only the inventoried fresh image is eligible; keep reservation through any partial firmware setup.
 itlIbssDataTrial.listenerQueueReserved=true;return awdlListenerQueueReady();
}
IOReturn ItlIwm::awdlListenerRequest(const ItlAwdl::SingleDataRequest &input){
 const auto result=awdlIbssDataRequest(input,false);
 // The caller retains the main gate; the worker cannot run before this mode commit.
 if(result==kIOReturnSuccess){itlIbssDataTrial.listener=true;awdlIbssDataPublish();}
 return result;
}
IOReturn ItlIwm::awdlListenerPairRequest(const ItlAwdl::SingleDataRequest &input,const uint8_t *other,const uint8_t *otherIp){
 if(!getMainWorkLoop()->inGate()||dmaDetaching)return kIOReturnNotReady;
 ItlAwdl::ListenerPairObserver pair;
 if(!pair.configure(input.peer,input.peerIp,other,otherIp,com.sc_ic.ic_myaddr,input.token))return kIOReturnBadArgument;
 const auto result=awdlListenerRequest(input);
 // Request and pair metadata commit in the same gate, before the worker can run.
 if(result==kIOReturnSuccess){itlIbssDataTrial.listenerPair=pair;awdlIbssDataPublish();}
 return result;
}
IOReturn ItlIwm::awdlIbssReceiveOnlyRequest(const ItlAwdl::SingleDataRequest &input){
 const auto result=awdlIbssDataRequest(input,false);
 if(result==kIOReturnSuccess){itlIbssDataTrial.passiveIbss=true;awdlIbssDataPublish();}
 return result;
}
bool ItlIwm::awdlIbssIdentityWindowValid(){
 auto &t=itlIbssDataTrial;
 if(t.controlSending){
  if(!awdlIbssDataActive()||!(t.controlMode&1)||t.passive()||t.cancelled||t.endRequested||
     !t.controlSchedule.armed||t.controlSchedule.stopped||t.controlSchedule.pending||
     t.controlSchedule.generation!=uint32_t(com.sc_generation)||
     (t.phase!=ItlIbss::DataTrialPhase::Receiving&&t.phase!=ItlIbss::DataTrialPhase::Submitted)||
     !getMainWorkLoop()->inGate()||getMainWorkLoop()->onThread())return false;
  const auto before=itlAwdlRocNowNs();if(!iwm_nic_lock(&com))return false;
  const auto gp2=iwm_read_prph(&com,IWM_DEVICE_SYSTEM_TIME_REG);iwm_nic_unlock(&com);const auto after=itlAwdlRocNowNs();
  ItlAwdl::FixedControlView view;
  return after>=before&&after-before<=2000000ULL&&gp2-t.controlGp2<=2000&&
   itlIbssFixedOwner==this&&itlIbssFixedControl.view(t.generation,gp2,after,view)&&
   !memcmp(view.source,t.request.peer,6)&&after<awdlIbssExclusive.deadline&&
   t.controlProof.generation==view.generation&&
   !memcmp(t.controlProof.election.masterAddress,view.election.masterAddress,6)&&
   !memcmp(t.controlProof.election.syncAddress,view.election.syncAddress,6)&&
   t.controlProof.election.masterMetric==view.election.masterMetric&&
   t.controlProof.election.masterCounter==view.election.masterCounter&&
   t.controlProof.election.height==view.election.height;
 }
 if(!awdlIbssDataActive()||t.passive()||!getMainWorkLoop()->inGate()||getMainWorkLoop()->onThread()||
    t.phase!=ItlIbss::DataTrialPhase::Receiving||(t.identityState!=1&&t.identityState!=3)||t.cancelled||t.endRequested)return false;
 const auto before=itlAwdlRocNowNs();if(!iwm_nic_lock(&com))return false;
 const auto gp2=iwm_read_prph(&com,IWM_DEVICE_SYSTEM_TIME_REG);iwm_nic_unlock(&com);const auto after=itlAwdlRocNowNs();
 ItlAwdl::DataContext c;awdlProbeCounters->dataModelView(c);c.gateHeld=true;c.generation=uint32_t(com.sc_generation);c.gp2=gp2;c.nowNs=after;
 const auto &w=awdlProbeCounters->ibss.window;
 return after>=before&&after-before<=2000000ULL&&gp2-t.identityTxGp2<=2000&&t.peerFresh(c)&&
  w.plan.epoch==c.modelEpoch&&w.inside(c.generation,gp2)&&ItlIbss::windowDelta(w.plan.close,gp2)>=5000;
}
int ItlIwm::awdlIbssDataIdentity(bool start){
 auto &t=itlIbssDataTrial;using D=ItlIbss::ExperimentObservation;
 if(!awdlIbssDataActive()||t.passive()||!getMainWorkLoop()->inGate()||getMainWorkLoop()->onThread()||
    t.phase!=ItlIbss::DataTrialPhase::Receiving||t.cancelled||t.endRequested)return EBUSY;
 const bool refresh=start&&t.identityState==2;
 if(refresh&&itlAwdlRocNowNs()<t.identityRefreshNotBeforeNs())return EAGAIN;
 if(t.identityState&&!refresh){
  if(awdlIbssManagementDrained()){
   if(t.identityState==1||t.identityState==3){++t.identityState;awdlIbssExperimentEvent(48,t.identityState,0,awdlProbeCounters->ibss.get(D::TxStatus),0);}
   return 0;
  }
  if(!awdlIbssTxPort||awdlIbssTxPort->mismatch||awdlProbeCounters->ibss.get(D::TxCompleted)||
     awdlProbeCounters->ibss.get(D::TxDeadlineExpired))return EIO;
  return EAGAIN;
 }
 if(!start)return EAGAIN;
 const auto before=itlAwdlRocNowNs();if(!iwm_nic_lock(&com))return EBUSY;
 const auto gp2=iwm_read_prph(&com,IWM_DEVICE_SYSTEM_TIME_REG);iwm_nic_unlock(&com);const auto after=itlAwdlRocNowNs();
 if(after<before||after-before>2000000ULL)return EBUSY;
 ItlAwdl::FixedControlView selected;
 if(itlIbssFixedOwner!=this||!itlIbssFixedControl.view(t.generation,gp2,after,selected)||
    memcmp(selected.source,t.request.peer,6))return EAGAIN;
 if(refresh){
  if(!awdlIbssManagementDrained()||awdlIbssPeerActive())return EBUSY;
  awdlIbssTxDispose();if(awdlIbssTxPort)return EBUSY;
  awdlProbeCounters->ibss.set(D::TxCompleted,0);awdlProbeCounters->ibss.set(D::TxReclaimed,0);
  awdlProbeCounters->ibss.set(D::TxStatus,0);awdlProbeCounters->ibss.set(D::TxAccepted,0);
  awdlProbeCounters->ibss.set(D::TxSubmitted,0);
 }
 if(awdlIbssTxPort||awdlIbssPeerActive()||ItlIbssTxPort::radioRejection(this,true))return EBUSY;
 t.identityState=refresh?3:1;t.identityTxGp2=gp2;
 if(!refresh){t.identityEpochGp2=gp2;t.identityEpochNs=before+(after-before)/2;}
 if(!awdlIbssIdentityWindowValid())return ESTALE;
 ItlAwdl::SelfControlInput input;memcpy(input.source,t.local,6);input.subtype=3;input.sequence=refresh?1:0;
 input.generation=t.generation;input.currentGeneration=uint32_t(com.sc_generation);
 input.epochGp2=t.identityEpochGp2;input.nowGp2=gp2;input.epochHostNs=t.identityEpochNs;input.nowHostNs=after;input.leaseEndNs=awdlIbssExclusive.deadline;
 uint8_t frame[ItlAwdl::kLinkIdentityLength];size_t size=0;
 ItlAwdl::FixedControlView proof;
 if(!ItlAwdl::buildFollowingLinkIdentity(input,itlIbssFixedControl,frame,sizeof(frame),size,proof))return EINVAL;
 auto p=new ItlIbssTxPort(this);if(!p)return ENOMEM;awdlIbssTxPort=p;++p->calls;
 p->record("AWDLIbssTxVersion",3);p->record("AWDLIbssTxGeneration",t.generation);
 awdlProbeCounters->ibss.set(D::TxAttempted,1);awdlProbeCounters->ibss.set(D::TxPayloadBytes,unsigned(size));
 const bool sent=p->adapter.start(frame,size);--p->calls;
 if(sent){t.identityMinEndNs=after+ItlAwdl::kSelfControlHorizonNs;itlIbssFixedAdvertised=proof;
  awdlIbssExperimentEvent(54,1,0,proof.aw,proof.down);
  awdlIbssExperimentEvent(54,2,0,proof.election.masterMetric,proof.election.masterCounter);
 }
 awdlIbssExperimentEvent(48,refresh?3:1,sent?0:EIO,unsigned(size),gp2);
 if(p->retiring)awdlIbssTxDispose();return sent?EAGAIN:EIO;
}
void ItlIwm::awdlIbssDataPublish(){
 if(itlIbssDataOwner!=this||!getMainWorkLoop()->inGate())return;
 auto &t=itlIbssDataTrial;auto &s=awdlIbssExclusive;
 if(t.active()&&t.phase!=ItlIbss::DataTrialPhase::WaitingSta&&!s.ownsRadio){
  t.phase=ItlIbss::DataTrialPhase::Recovering;
  if(!awdlProbeCounters->ibss.recovery.watching()&&(awdlProbeCounters->ibss.recovery.phase!=ItlIbss::StaRecoveryPhase::Idle||s.phase==ItlIbss::ExclusivePhase::Failed||s.phase==ItlIbss::ExclusivePhase::Cancelled))
   t.finishRecovery(s.phase==ItlIbss::ExclusivePhase::Associated&&
    awdlProbeCounters->ibss.recovery.phase==ItlIbss::StaRecoveryPhase::StableAtDeadline&&
    !s.primaryError&&!s.recoveryError&&!awdlProbeCounters->ibss.control.lost&&
    !awdlProbeCounters->ibss.get(ItlIbss::ExperimentObservation::RecoveryFaults));
 }
 OSDictionary *d=OSDictionary::withCapacity(101);if(!d)return;
 const char *names[]={"Version","Token","Phase","Result","EverArmed","Prepared","Completed","AckReported","RawStatus","StopReclaimed","Recovered","Generation","Queue","Slot","Block","Cancelled","Submitted","EndRequested"};
 const uint64_t values[]={1,t.request.token,uint32_t(t.phase),uint32_t(t.result),t.everArmed,t.prepared,t.completed,t.ack,t.rawStatus,t.stopReclaimed,t.recovered,t.generation,t.queue,t.slot,t.block,t.cancelled,t.sentNs!=0,t.endRequested};
 bool ok=true;for(unsigned i=0;i<18;++i){OSNumber *n=OSNumber::withNumber(values[i],64);if(!n){ok=false;break;}ok=d->setObject(names[i],n)&&ok;n->release();}
 const char *controlNames[]={"ControlMode","ControlScheduleVersion","ControlSubmitted","ControlCompleted","ControlPsf","ControlMif","ControlAwaitingObservation","ControlStopped","ControlBlocked","PeerHtObserved","PeerHtApplied","PeerHtCapabilities","PeerHtMcs","PeerRxHtFrames","PeerRxOtherRateFrames","PeerRxLastRateFlags"};
 const uint64_t controlValues[]={t.controlMode,1,t.controlSchedule.submitted,t.controlSchedule.completed,t.controlSchedule.psf,t.controlSchedule.mif,t.controlSchedule.pending,t.controlSchedule.stopped,t.controlBlocked,
  t.peerCapabilities.generation!=0,t.peerCapabilitiesApplied,t.peerCapabilities.ht,t.peerCapabilities.mcs,t.peerRxHt,t.peerRxOtherRate,t.peerRxLastRate};
 for(unsigned i=0;i<16&&ok;++i){auto n=OSNumber::withNumber(controlValues[i],64);if(!n){ok=false;break;}ok=d->setObject(controlNames[i],n);n->release();}
 const char *pairNames[]={"PairFrames","PairNoMsdu","PairProtected","PairFcsVerified","PairFcsUnverified","PairDecodeRejected","PairIpRejected","PairChecksumRejected","PairMarkerRejected","PairIdentifierRejected","PairEchoRequests","PairEchoReplies","PairMatchedFrames","PairDuplicates"};
 static_assert(sizeof(pairNames)/sizeof(pairNames[0])==ItlAwdl::PairRxMetricCount,"pair metric names");
 for(unsigned i=0;i<ItlAwdl::PairRxMetricCount&&ok;++i){auto n=OSNumber::withNumber(t.listenerPair.counts[i],64);if(!n){ok=false;break;}ok=d->setObject(pairNames[i],n);n->release();}
 const char *pairExtraNames[]={"PairObserverVersion","PairConfigured","PairAtoBRequestMask","PairAtoBReplyMask","PairBtoARequestMask","PairBtoAReplyMask","PairIdentifierKnown","PairIdentifier","PairLastCode","PairLastSequence","PairLastPayloadHash"};
 const auto &pair=t.listenerPair;const uint64_t pairExtra[]={1,pair.configured,pair.seen[0],pair.seen[1],pair.seen[2],pair.seen[3],pair.identifierKnown,pair.identifier,pair.lastCode,pair.lastSequence,pair.lastPayloadHash};
 for(unsigned i=0;i<11&&ok;++i){auto n=OSNumber::withNumber(pairExtra[i],64);if(!n){ok=false;break;}ok=d->setObject(pairExtraNames[i],n);n->release();}
 const auto &fixed=itlIbssFixedAdvertised;
 const bool fixedCurrent=itlIbssFixedOwner==this&&fixed.generation==t.generation&&fixed.generation;
 uint64_t fixedMaster=0,fixedSync=0;
 if(fixedCurrent)for(unsigned k=0;k<6;++k){fixedMaster=(fixedMaster<<8)|fixed.election.masterAddress[k];fixedSync=(fixedSync<<8)|fixed.election.syncAddress[k];}
 const char *replyNames[]={"ReplyObserverVersion","ReplyObserved","ReplyListenEndNs","IdentityMifState","RxDiagnosticsVersion","RawDataFrames","CommonDataFrames","PeerDataFrames","ManualRepeatVersion","Run","IdentityRefreshVersion","IdentityRefreshCompleted","IdentityRefreshGp2","FixedControlVersion","FixedControlGeneration","FixedControlMaster","FixedControlSync","FixedControlHeight","FixedControlMetric","FixedControlCounter"};
 const uint64_t replyValues[]={1,t.replyObserver.replyObserved,t.replyEndNs,t.identityState,1,t.rawDataFrames,t.commonDataFrames,t.peerDataFrames,1,t.run,1,t.identityState==4,t.identityTxGp2,1,fixedCurrent?fixed.generation:0,fixedMaster,fixedSync,fixedCurrent?fixed.election.height:0,fixedCurrent?fixed.election.masterMetric:0,fixedCurrent?fixed.election.masterCounter:0};
 for(unsigned i=0;i<20&&ok;++i){auto n=OSNumber::withNumber(replyValues[i],64);if(!n){ok=false;break;}ok=d->setObject(replyNames[i],n);n->release();}
 const char *stageNames[]={"PeerRawIngress","PeerRejectCrc","PeerRejectLength","PeerRejectPhy","PeerRejectDecrypt","PeerRejectDuplicate","PeerReorderConsumed","PeerCommonFrame","PeerRejectBuffer"};
 const char *replyStageNames[]={"ReplyPeerRawIngress","ReplyPeerRejectCrc","ReplyPeerRejectLength","ReplyPeerRejectPhy","ReplyPeerRejectDecrypt","ReplyPeerRejectDuplicate","ReplyPeerReorderConsumed","ReplyPeerCommonFrame","ReplyPeerRejectBuffer"};
 static_assert(sizeof(stageNames)/sizeof(stageNames[0])==ItlAwdl::RawStageCount,"RX stage property count");
 for(unsigned i=0;i<ItlAwdl::RawStageCount&&ok;++i){auto a=OSNumber::withNumber(t.peerRxStages[i],64);auto b=OSNumber::withNumber(t.replyPeerRxStages[i],64);
  if(!a||!b)ok=false;else ok=d->setObject(stageNames[i],a)&&d->setObject(replyStageNames[i],b);
  if(a)a->release();if(b)b->release();}
 if(ok){auto n=OSNumber::withNumber(1,64);if(!n)ok=false;else{ok=d->setObject("PeerRxStageVersion",n);n->release();}}
 const char *coverageNames[]={"RxCoveragePackets","RxCoverageInvalidDescriptor","RxCoverageInvalidLength","RxCoverageInvalidFrameControl","RxCoverageDataWithoutAddresses","RxCoverageDataWithAddresses","RxCoverageLocalReceiver","RxCoveragePeerTransmitter","RxCoveragePeerToLocal","RxCoveragePeerToOtherReceiver","RxCoveragePeerOnChannel6","RxCoveragePeerOnOtherChannel","RxCoveragePeerOnUnknownChannel"};
 const char *replyCoverageNames[]={"ReplyRxCoveragePackets","ReplyRxCoverageInvalidDescriptor","ReplyRxCoverageInvalidLength","ReplyRxCoverageInvalidFrameControl","ReplyRxCoverageDataWithoutAddresses","ReplyRxCoverageDataWithAddresses","ReplyRxCoverageLocalReceiver","ReplyRxCoveragePeerTransmitter","ReplyRxCoveragePeerToLocal","ReplyRxCoveragePeerToOtherReceiver","ReplyRxCoveragePeerOnChannel6","ReplyRxCoveragePeerOnOtherChannel","ReplyRxCoveragePeerOnUnknownChannel"};
 static_assert(sizeof(coverageNames)/sizeof(coverageNames[0])==ItlIbss::RxCoverageCount,"RX coverage property count");
 static_assert(sizeof(replyCoverageNames)/sizeof(replyCoverageNames[0])==ItlIbss::RxCoverageCount,"reply RX coverage property count");
 for(unsigned i=0;i<ItlIbss::RxCoverageCount&&ok;++i){auto a=OSNumber::withNumber(t.rxCoverage[i],64);auto b=OSNumber::withNumber(t.replyRxCoverage[i],64);
  if(!a||!b)ok=false;else ok=d->setObject(coverageNames[i],a)&&d->setObject(replyCoverageNames[i],b);
  if(a)a->release();if(b)b->release();}
 if(ok){auto n=OSNumber::withNumber(1,64);if(!n)ok=false;else{ok=d->setObject("RxCoverageVersion",n);n->release();}}
 const char *ingressNames[]={"RxIngressBuffersSeen","RxIngressBuffersCaptured","RxIngressExcludedOwner","RxIngressExcludedGeneration","RxIngressExcludedWindow","RxIngressDmaFailures","RxIngressNotifications","RxIngressMpduNotifications","RxIngressEmptyHeaders","RxIngressSentinels","RxIngressInvalidLengths","RxIngressBufferEnds"};
 const char *replyIngressNames[]={"ReplyRxIngressBuffersSeen","ReplyRxIngressBuffersCaptured","ReplyRxIngressExcludedOwner","ReplyRxIngressExcludedGeneration","ReplyRxIngressExcludedWindow","ReplyRxIngressDmaFailures","ReplyRxIngressNotifications","ReplyRxIngressMpduNotifications","ReplyRxIngressEmptyHeaders","ReplyRxIngressSentinels","ReplyRxIngressInvalidLengths","ReplyRxIngressBufferEnds"};
 static_assert(sizeof(ingressNames)/sizeof(ingressNames[0])==ItlIbss::RxIngressDiagnostics::Count,"RX ingress property count");
 static_assert(sizeof(replyIngressNames)/sizeof(replyIngressNames[0])==ItlIbss::RxIngressDiagnostics::Count,"reply RX ingress property count");
 for(unsigned i=0;i<ItlIbss::RxIngressDiagnostics::Count&&ok;++i){
  auto a=OSNumber::withNumber(t.ingress.total[i],64);auto b=OSNumber::withNumber(t.ingress.reply[i],64);
  if(!a||!b)ok=false;else ok=d->setObject(ingressNames[i],a)&&d->setObject(replyIngressNames[i],b);
  if(a)a->release();if(b)b->release();
 }
 const char *faultNames[]={"RxIngressVersion","RxIngressFirstFault","RxIngressFirstOffset","RxIngressFirstDetail","RxIngressFirstHostNs"};
 const uint64_t faultValues[]={1,t.ingress.firstFault,t.ingress.firstOffset,t.ingress.firstDetail,t.ingress.firstNs};
 for(unsigned i=0;i<5&&ok;++i){auto n=OSNumber::withNumber(faultValues[i],64);if(!n){ok=false;break;}ok=d->setObject(faultNames[i],n);n->release();}

 auto echoProfile=OSNumber::withNumber(t.echo.profile,32);if(echoProfile){d->setObject("EchoProfile",echoProfile);echoProfile->release();}else ok=false;
 const char *injectionNames[]={"ListenerInjectionMode","ListenerInjectionStage","ListenerInjectionCompleted","ListenerInjectionStatus"};
 const uint64_t injectionValues[]={t.listenerTx,t.listenerTxStage,t.listenerTxCompletions,t.listenerTxStatus};
 for(unsigned i=0;i<4&&ok;++i){auto n=OSNumber::withNumber(injectionValues[i],64);if(!n){ok=false;break;}ok=d->setObject(injectionNames[i],n);n->release();}
 const char *listenerNames[]={"ListenerSchema","PassiveListener","ListenerQueueReserved","ListenerConfigured","ListenerFinished","ListenerPeerCommonData","ListenerObserveNs"};
 const uint64_t listenerValues[]={1,t.listener,t.listenerQueueReserved,t.listenerConfigured,t.listenerFinished,t.listenerPeerCommonData,t.listenerObserveNs};
 for(unsigned i=0;i<7&&ok;++i){auto n=OSNumber::withNumber(listenerValues[i],64);if(!n)ok=false;else{ok=d->setObject(listenerNames[i],n);n->release();}}
 const char *targetNames[]={"TargetRxFrames","TargetRxNoMsdu","TargetRxProtected","TargetRxFcsVerified","TargetRxFcsUnverified","TargetRxLayoutRejected","TargetRxProtocolRejected","TargetRxLengthRejected","TargetRxAddressRejected","TargetRxIpv6","TargetRxIcmpChecksumRejected","TargetRxEchoRequest","TargetRxEchoReply","TargetRxStimulusMatched"};
 static_assert(sizeof(targetNames)/sizeof(targetNames[0])==ItlAwdl::TargetRxMetricCount,"target observer property count");
 for(unsigned i=0;i<ItlAwdl::TargetRxMetricCount&&ok;++i){auto n=OSNumber::withNumber(t.targetRx.counts[i],64);if(!n)ok=false;else{ok=d->setObject(targetNames[i],n);n->release();}}
 const char *extraNames[]={"TargetRxVersion","TargetRxLastIdentifier","TargetRxLastSequence","PassiveIbss","PassiveReceiveMode"};
 const uint64_t extraValues[]={1,t.targetRx.lastIdentifier,t.targetRx.lastSequence,t.passiveIbss,t.listener?2u:(t.passiveIbss?1u:0u)};
 for(unsigned i=0;i<5&&ok;++i){auto n=OSNumber::withNumber(extraValues[i],64);if(!n)ok=false;else{ok=d->setObject(extraNames[i],n);n->release();}}
 const char *batchNames[]={"SmallBatchVersion","SmallBatchEnabled","SmallBatchIndex","SmallBatchSubmittedMask","SmallBatchCompletedMask","SmallBatchReplyMask","SmallBatchEndNs","SmallBatchCurrentToken"};
 const uint64_t batchValues[]={1,t.smallBatch.enabled,t.smallBatch.index,t.smallBatch.submitted,t.smallBatch.completed,t.smallBatch.replied,t.smallBatch.endNs,t.currentRequest().token};
 for(unsigned i=0;i<8&&ok;++i){auto n=OSNumber::withNumber(batchValues[i],64);if(!n)ok=false;else{ok=d->setObject(batchNames[i],n);n->release();}}
 const char *nativeNames[]={"NativeQueueVersion","NativeQueueMode","NativeAttempted","NativeStarted","NativeDeadlineReached","NativeError"};
 const uint64_t nativeValues[]={1,t.nativeQueueMode,t.nativeAttempted,t.nativeStarted,t.nativeDeadlineReached,t.nativeError};
 for(unsigned i=0;i<6&&ok;++i){auto n=OSNumber::withNumber(nativeValues[i],64);if(!n)ok=false;else{ok=d->setObject(nativeNames[i],n);n->release();}}
 const char *nativeHealthNames[]={"NativeLeaseReject","NativeDeferrals","NativeStartNs","NativeStopNs","NativeFirstDeferredNs","NativeTeardownComplete"};
 const uint64_t nativeHealthValues[]={t.nativeLeaseReject,t.nativeDeferrals,t.nativeStartNs,t.nativeStopNs,t.nativeFirstDeferredNs,t.nativeTeardownComplete};
 for(unsigned i=0;i<6&&ok;++i){auto n=OSNumber::withNumber(nativeHealthValues[i],64);if(!n)ok=false;else{ok=d->setObject(nativeHealthNames[i],n);n->release();}}
 const char *nativeTxNames[]={"NativeTxTicks","NativeQueuedTicks","NativeWindowDenied","NativeTaken","NativeFrameStarted","NativeFrameCompleted"};
 const uint64_t nativeTxValues[]={t.nativeTxTicks,t.nativeQueuedTicks,t.nativeWindowDenied,t.nativeTaken,t.nativeFrameStarted,t.nativeFrameCompleted};
 for(unsigned i=0;i<6&&ok;++i){auto n=OSNumber::withNumber(nativeTxValues[i],64);if(!n)ok=false;else{ok=d->setObject(nativeTxNames[i],n);n->release();}}
 if(ok)getController()->setProperty("AWDLIbssDataTrial",d);d->release();
}
IOReturn ItlIwm::awdlIbssEchoRequest(const ItlAwdl::SingleDataRequest &input,unsigned mode,unsigned profile){
 if(!getMainWorkLoop()->inGate()||profile<1||profile>3||mode>3)return kIOReturnBadArgument;
 const auto result=awdlIbssDataRequest(input,false,mode);
 if(result==kIOReturnSuccess){itlIbssDataTrial.echo.profile=profile;awdlIbssDataPublish();}
 return result;
}
IOReturn ItlIwm::awdlIbssDataRequest(const ItlAwdl::SingleDataRequest &input,bool cancel,unsigned mode,bool nativeRequest){
 if(!getMainWorkLoop()->inGate()||dmaDetaching||!awdlProbeCounters||awdlNativeSearchReserved())return kIOReturnNotReady;
 if(mode>4|| (cancel&&mode)||(nativeRequest&&(cancel||mode!=3)))return kIOReturnBadArgument;
 auto &t=itlIbssDataTrial;using P=ItlIbss::DataTrialPhase;
 if(cancel){
  if(itlIbssDataOwner!=this||!t.active())return kIOReturnNotReady;
  t.cancelled=t.endRequested=true;t.result=ItlIbss::DataTrialResult::Cancelled;
  getMainCommandGate()->commandWakeup(&awdlIbssWorkPending,false);awdlIbssDataPublish();return kIOReturnSuccess;
 }
 ItlIbssExclusiveTransport transport{this};
 const bool repeat=t.everArmed;
 auto &exclusive=awdlIbssExclusive;auto &previous=awdlProbeCounters->ibss;
 // Record the first failing gate without arming/resetting a trial or consuming RX rows.
 auto reject=[&](unsigned reason)->IOReturn{
  auto d=OSDictionary::withCapacity(9);
  if(d){
   const char *names[]={"Version","Token","Mode","Reason","Generation","HostNs","WorkerPending","ExclusivePhase","FirmwareFlags"};
   const uint64_t values[]={1,input.token,mode,reason,uint32_t(com.sc_generation),itlAwdlRocNowNs(),
    awdlIbssWorkPending,uint32_t(exclusive.phase),uint32_t(com.sc_flags)};
   bool ok=true;
   for(unsigned i=0;i<9&&ok;++i){auto n=OSNumber::withNumber(values[i],64);
    if(!n){ok=false;break;}ok=d->setObject(names[i],n);n->release();}
   if(ok)getController()->setProperty("AWDLIbssDataAdmission",d);d->release();
  }
  return kIOReturnNotReady;
 };
 if(repeat){
  if(itlIbssDataOwner!=this)return reject(1);
  const bool cancelledNative=nativeRequest&&t.mayRestartCancelledNative(input.token);
  if(!t.mayRepeat(input.token)&&!cancelledNative)return reject(2);
  const bool untouched=cancelledNative&&!awdlIbssOneShot.attempted;
  if(untouched){
   if(t.generation||t.nativeStarted||!ItlIbss::untouchedDataTrialReusable(exclusive))return reject(3);
   if(previous.recovery.phase!=ItlIbss::StaRecoveryPhase::Idle)return reject(4);
  }else{
   if(!t.recovered||!ItlIbss::recoveredDataTrialReusable(exclusive))return reject(3);
   if(previous.recovery.phase!=ItlIbss::StaRecoveryPhase::StableAtDeadline)return reject(4);
  }
  if(previous.control.lost)return reject(5);
  if(previous.get(ItlIbss::ExperimentObservation::RecoveryFaults))return reject(6);
  if(!untouched&&!awdlIbssOneShot.attempted)return reject(7);
 }
 if(nativeRequest&&!awdlIbssNativeStartIdle())return reject(22);
 if(awdlBatch.enabled||awdlBatch.active())return reject(8);
 if(!repeat&&awdlIbssOneShot.armed)return reject(9);
 if(awdlIbssWorkPending)return reject(10);
 if(awdlIbssTxPort)return reject(11);
 if(awdlIbssPeerActive())return reject(12);
 if(!repeat&&exclusive.phase!=ItlIbss::ExclusivePhase::Idle)return reject(13);
 if(exclusive.operation)return reject(14);
 if(exclusive.ordinary.active())return reject(15);
 if(!awdlProbeEnabled)return reject(16);
 if(ItlAwdl::gEngine.running)return reject(17);
 if(ItlAwdl::gSingleDataProbe.pending)return reject(18);
 if(!transport.canBegin())return reject(19);
 if(!transport.administrativelyUp())return reject(20);
 if(!transport.associatedAndAuthorized())return reject(21);
 auto request=input;memset(request.sourceIp,0,16);request.sourceIp[0]=0xfe;request.sourceIp[1]=0x80;
 const auto local=com.sc_ic.ic_myaddr;request.sourceIp[8]=local[0]^2;request.sourceIp[9]=local[1];request.sourceIp[10]=local[2];
 request.sourceIp[11]=0xff;request.sourceIp[12]=0xfe;request.sourceIp[13]=local[3];request.sourceIp[14]=local[4];request.sourceIp[15]=local[5];
 ItlIbss::DataTrial next;if(!next.arm(request,local,uint32_t(com.sc_generation),itlAwdlRocNowNs()))return kIOReturnBadArgument;
 next.nativeQueueMode=nativeRequest;next.run=repeat?t.run+1:1;next.controlMode=mode==4?3:mode;next.smallBatch.enabled=mode==4;
 auto counters=new ItlAwdlProbeCounters;if(!counters||!counters->init()){if(counters)counters->release();return kIOReturnNoMemory;}
 const auto timerReady=awdlIbssExperimentTimerPrepare();
 if(timerReady!=kIOReturnSuccess){counters->release();return timerReady;}
 using D=ItlIbss::ExperimentObservation;auto old=awdlProbeCounters;
 counters->configureManagementFilter(awdlMgmtFilterEnabled);
 for(unsigned i=D::NvmChannelFlagsVersion;i<=D::MccChubSource;++i)counters->ibss.set(static_cast<D::Metric>(i),old->ibss.get(static_cast<D::Metric>(i)));
 counters->ibss.set(D::FirmwareFaultTraceVersion,1);counters->ibss.set(D::RecoveryObserverVersion,2);
 if(repeat){auto record=getController()->getProperty("AWDLIbssDataTrial");
  if(!record||!getController()->setProperty("AWDLPreviousIbssDataTrial",record)){counters->release();return kIOReturnNoMemory;}}
 if(!getController()->setProperty("AWDLBeforeIbssData",old)||!getController()->setProperty("AWDLRxProbe",counters)){counters->release();return kIOReturnNoMemory;}
 // No firmware waits/gate release between admission checks and this metadata commit.
 if(repeat){exclusive.phase=ItlIbss::ExclusivePhase::Idle;exclusive.resetProven=false;
  exclusive.generation=0;exclusive.deadline=exclusive.recoveryDeadline=0;}
 __atomic_store_n(&awdlProbeCounters,counters,__ATOMIC_SEQ_CST);old->release();
 t=next;itlIbssDataOwner=this;itlIbssFixedOwner=nullptr;itlIbssFixedGeneration=0;itlIbssFixedAdvertised={};awdlIbssOneShot=ItlIbss::OneShot();awdlIbssOneShot.armed=awdlIbssOneShot.profile=true;
 counters->ibss.set(D::Armed,1);counters->ibss.set(D::ProfileRequested,1);
 awdlIbssExperimentTimerStart();
 if(!awdlIbssTimer){t.phase=P::Failed;t.result=ItlIbss::DataTrialResult::PrepareFailed;awdlIbssDataPublish();return kIOReturnNoResources;}
 awdlIbssDataPublish();return kIOReturnSuccess;
}
bool ItlIwm::awdlIbssDataMayTransmit(const ItlIbss::PeerGrant &grant){
 if(itlNativeSearchPeerOwned(this))return getMainWorkLoop()->inGate()&&itlNativeSearchPeerMayTransmit(this,grant);
 if(itlIbssDataTrial.nativeQueueMode)return itlIbssNativeMayTransmit(this,grant);
 if(!awdlIbssDataActive()||itlIbssDataTrial.passive()||!getMainWorkLoop()->inGate()||!awdlProbeCounters||
    !ItlIbss::samePeerGrant(grant,itlIbssPeerPort?itlIbssPeerPort->reserved:ItlIbss::PeerGrant())||
    memcmp(grant.local,itlIbssDataTrial.local,6)||memcmp(grant.peer,itlIbssDataTrial.request.peer,6))return false;
 const auto before=itlAwdlRocNowNs();if(!iwm_nic_lock(&com))return false;
 const auto gp2=iwm_read_prph(&com,IWM_DEVICE_SYSTEM_TIME_REG);iwm_nic_unlock(&com);const auto after=itlAwdlRocNowNs();
 if(after<before||after-before>2000000ULL)return false;
 ItlAwdl::DataContext c;awdlProbeCounters->dataModelView(c);c.gateHeld=true;c.generation=uint32_t(com.sc_generation);
 c.gp2=gp2;c.nowNs=after;c.deadlineNs=awdlIbssExclusive.deadline;c.channel=6;
 return itlIbssFixedDataAllowed(this,c.generation,c.gp2,c.nowNs)&&itlIbssDataTrial.mayTransmit(c,awdlProbeCounters->ibss.window);
}
void ItlIwm::awdlIbssDataPortResult(bool stopped){
 if(itlIbssDataOwner!=this||!getMainWorkLoop()->inGate())return;
 auto p=itlIbssPeerPort;if(!p||p->hal!=this)return;auto &t=itlIbssDataTrial;const auto time=itlAwdlRocNowNs();
 if(t.nativeQueueMode){
  awdlIbssExperimentEvent(58,stopped?3:2,0,p->data.rawStatus(),p->noticeSlot);return;
 }
 if(p->data.completed()){
 if(t.smallBatch.enabled&&!t.completed)t.smallBatch.markCompleted();
 t.completed=true;t.ack=p->data.ackReported();t.rawStatus=p->data.rawStatus();if(!t.cancelled&&t.result==ItlIbss::DataTrialResult::None){
  if(!t.sentNs||time<t.sentNs||time-t.sentNs>=2000000000ULL)t.result=ItlIbss::DataTrialResult::TimedOut;
  else t.result=t.ack?ItlIbss::DataTrialResult::AckReported:ItlIbss::DataTrialResult::NoAck;
 }
 // Reclaim TX DMA immediately; preserve the original bounded reply deadline.
 if(stopped||!t.ack||t.result!=ItlIbss::DataTrialResult::AckReported||!t.replyEndNs)t.endRequested=true;
 }
 if(stopped&&t.sentNs&&!t.completed)t.stopReclaimed=true;
 if(p->mismatch){t.block=7;t.endRequested=true;}
 using H=ItlIbss::ExperimentRows;H::Row row;const auto us=time/1000;
 row.v[H::Kind]=40;row.v[H::Code]=stopped?3:2;row.v[H::HostUsLow]=uint32_t(us);row.v[H::HostUsHigh]=uint32_t(us>>32);
 row.v[H::Generation]=t.generation;row.v[H::Phase]=uint32_t(t.phase);row.v[H::Status]=t.rawStatus;row.v[H::Flags]=(t.completed?1u:0u)|(t.ack?2u:0u)|(p->data.pending()?4u:0u)|(p->mismatch?8u:0u);row.v[H::Channel]=p->reserved.queue;row.v[H::MacContext]=p->noticeSlot;row.v[H::Length]=p->noticeLength;row.v[H::FrameControl]=p->noticeCount;row.v[H::Detail0]=p->noticeSsn;row.v[H::Detail1]=p->noticeStatus;
 awdlProbeCounters->ibss.control.append(row);getMainCommandGate()->commandWakeup(&awdlIbssWorkPending,false);
}
void ItlIwm::awdlIbssDataTick(){
 if(!awdlIbssDataActive()||!getMainWorkLoop()->inGate()||getMainWorkLoop()->onThread())return;
 auto &t=itlIbssDataTrial;auto &s=awdlIbssExclusive;using P=ItlIbss::DataTrialPhase;using R=ItlIbss::DataTrialResult;
 const auto now=itlAwdlRocNowNs();
 if(t.phase==P::WaitingSta&&!awdlIbssOneShot.attempted){
  if(now<t.baselineLastNs||now-t.baselineLastNs>2000000000ULL){awdlIbssOneShot.stableSince=0;awdlIbssOneShot.stableGeneration=0;}
  t.baselineLastNs=now;
  if(t.cancelled||now>=t.deadlineNs){awdlIbssOneShot.armed=false;t.phase=t.cancelled?P::Cancelled:P::Failed;t.result=t.cancelled?R::Cancelled:R::TimedOut;awdlIbssDataPublish();}return;
 }
 if(!s.ownsRadio){if(t.phase==P::WaitingSta)t.phase=P::Recovering;if(t.result==R::None)t.result=R::NoWindow;awdlIbssDataPublish();return;}
 if(s.phase==ItlIbss::ExclusivePhase::Failed){t.phase=P::Recovering;t.result=R::RecoveryFailed;t.block=8;awdlIbssDataPublish();return;}
 if(s.phase!=ItlIbss::ExclusivePhase::FirmwareReady||s.operation)return;
 if(t.phase==P::WaitingSta){t.phase=P::Receiving;t.generation=uint32_t(com.sc_generation);}
 if(t.nativeQueueMode&&t.prepared){
  if(t.cancelled||t.endRequested||s.resetRequested||now>=s.deadline){
   t.controlSchedule.stop();
   if(!t.cancelled&&!s.resetRequested&&now<t.identityMinEndNs&&now<s.deadline){awdlIbssDataPublish();return;}
   t.phase=P::Recovering;awdlIbssExclusiveEnd();
  }else if(!t.nativeAttempted){
   if(t.controlSchedule.pending){awdlIbssDataPublish();return;}
   t.nativeAttempted=true;
   if(awdlIbssNativeOpen(t.local)){t.nativeStarted=true;t.nativeStartNs=itlAwdlRocNowNs();t.phase=P::Submitted;}
   else{t.nativeError=EIO;t.result=R::PrepareFailed;t.endRequested=true;}
  }else awdlIbssScheduledTick();
  awdlIbssDataPublish();return;
 }
 if(t.listenerTx){
  if(s.session.quotaConfigured&&!s.session.needsReset&&!t.listenerConfigured){t.listenerConfigured=true;t.listenerStartNs=now;}
  if(t.listenerStartNs&&now>=t.listenerStartNs){const auto end=now<s.deadline?now:s.deadline;t.listenerObserveNs=end>t.listenerStartNs?end-t.listenerStartNs:0;}
  awdlListenerTxTick();awdlIbssDataPublish();return;
 }
 if(t.passive()){
  if(s.session.quotaConfigured&&!s.session.needsReset&&!t.listenerConfigured){t.listenerConfigured=true;t.listenerStartNs=now;}
  if(t.listenerStartNs&&now>=t.listenerStartNs){const auto end=now<s.deadline?now:s.deadline;t.listenerObserveNs=end>t.listenerStartNs?end-t.listenerStartNs:0;}
  if(t.cancelled||s.resetRequested||now>=s.deadline){
   t.listenerFinished=t.listenerConfigured&&t.listenerObserveNs&&!t.cancelled&&!s.resetRequested&&!s.session.needsReset;
   t.result=t.cancelled?R::Cancelled:(t.listenerFinished?R::PassiveComplete:R::RecoveryFailed);
   t.phase=P::Recovering;awdlIbssExclusiveEnd();
  }
  awdlIbssDataPublish();return;
 }
 if(t.phase==P::Submitted&&!t.completed&&now-t.sentNs>=2000000000ULL){t.result=R::TimedOut;t.endRequested=true;}
 if(t.smallBatch.enabled&&t.smallBatch.endNs&&now>=t.smallBatch.endNs&&!t.smallBatch.success()){
  t.result=R::TimedOut;t.endRequested=true;
 }
 if(t.smallBatch.mayAdvance(now)&&t.phase==P::Submitted&&t.completed&&t.ack&&
    !t.cancelled&&!t.endRequested&&!s.resetRequested&&now>=t.sentNs&&now-t.sentNs>=10000000ULL&&
    !t.controlSchedule.pending){
  auto next=t.currentRequest();next.token=ItlAwdl::SmallDataBatch::tokenAt(t.request.token,t.smallBatch.index+1);
  uint8_t bytes[104];size_t length=0;
  if(!ItlAwdl::buildSingleDataEcho(next,t.local,t.generation,uint16_t(t.smallBatch.index+2),bytes,sizeof(bytes),length)||
     !itlIbssPeerNext(this,bytes,length)||!t.smallBatch.advance(now)){
   t.result=R::PrepareFailed;t.endRequested=true;
  }else{
   // New packet gets a fresh prospective window; the original lease and three-packet cap remain.
   awdlProbeCounters->ibss.window={};t.phase=P::WaitingWindow;t.sentNs=0;
   t.completed=t.ack=false;t.rawStatus=UINT32_MAX;t.replyObserver={};t.result=R::None;
   awdlIbssExperimentEvent(56,t.smallBatch.index,0,t.smallBatch.submitted,t.smallBatch.replied);
  }
 }
 // Scheduled modes keep advertising after the first reply, within the existing lease/window.
 if(t.phase==P::Submitted&&t.completed&&t.ack&&
    ((!(t.controlMode&1)&&t.replyObserver.replyObserved)||now>=t.replyEndNs))t.endRequested=true;
 if(t.endRequested&&!t.cancelled&&!s.resetRequested&&now<t.identityMinEndNs&&now<s.deadline){awdlIbssDataPublish();return;}
 if(t.cancelled||t.endRequested||s.resetRequested||now>=s.deadline){
  t.controlSchedule.stop();
  if(t.result==R::None)t.result=R::NoWindow;t.phase=P::Recovering;awdlIbssExclusiveEnd();awdlIbssDataPublish();return;
 }
 if((t.controlMode&1)&&t.identityState==4){
  awdlIbssScheduledTick();
  if(t.endRequested||(t.phase==P::Receiving&&t.controlSchedule.pending)){awdlIbssDataPublish();return;}
 }
 if(t.phase==P::Receiving&&s.session.quotaConfigured){
  const int identity=awdlIbssDataIdentity(false);
  if(identity){if(identity!=EAGAIN){t.block=9;t.result=R::PrepareFailed;t.endRequested=true;}awdlIbssDataPublish();return;}
  if(t.identityState!=4){awdlIbssDataPublish();return;}
  if((t.controlMode&1)&&!t.controlSchedule.armed){awdlIbssScheduledTick();awdlIbssDataPublish();return;}
  if(!iwm_nic_lock(&com))return;const auto gp2=iwm_read_prph(&com,IWM_DEVICE_SYSTEM_TIME_REG);iwm_nic_unlock(&com);
  ItlAwdl::DataContext c;awdlProbeCounters->dataModelView(c);c.gateHeld=true;c.generation=uint32_t(com.sc_generation);c.gp2=gp2;c.nowNs=now;
  if(!t.peerFresh(c)){t.block=1;return;}
  t.block=0;uint8_t bytes[106];size_t length=0;
  const bool built=t.echo.build(t.request,t.local,t.generation,1,bytes,sizeof(bytes),length);
  if(!built)awdlIbssExperimentEvent(41,20,EINVAL,0,0);
  if(!built||!awdlIbssPeerPrepare(bytes,length)){
   t.block=2;t.result=R::PrepareFailed;t.endRequested=true;return;
  }
  if(t.cancelled||!s.ownsRadio||uint32_t(com.sc_generation)!=t.generation){t.endRequested=true;return;}
  t.prepared=true;t.phase=P::WaitingWindow;t.block=0;
 }
 awdlIbssDataPublish();
}
void ItlIwm::awdlIbssDataWindow(){
 if(!awdlIbssDataActive()||itlIbssDataTrial.passive()||!getMainWorkLoop()->inGate()||getMainWorkLoop()->onThread())return;
 auto &t=itlIbssDataTrial;using P=ItlIbss::DataTrialPhase;
 if(t.phase==P::Receiving&&(t.identityState==0||t.identityState==2)&&awdlProbeCounters->ibss.window.phase==ItlIbss::WindowProbe::Open){
  const int error=awdlIbssDataIdentity(true);
  if(error!=EAGAIN){t.block=9;t.result=ItlIbss::DataTrialResult::PrepareFailed;t.endRequested=true;}
  awdlIbssDataPublish();return;
 }
 if(t.nativeQueueMode){itlIbssNativeTick(this);return;} // Poll system packets after the fresh window transition; never inject diagnostic Echo.
 if(t.phase!=P::WaitingWindow||awdlProbeCounters->ibss.window.phase!=ItlIbss::WindowProbe::Open)return;
 // Never defer a refreshed advertisement into a later window and call it fresh.
 const auto now=itlAwdlRocNowNs();
 const auto refreshed=t.identityEpochNs+uint64_t(uint32_t(t.identityTxGp2-t.identityEpochGp2))*1000;
 // Later batch packets use the fresh periodic control proof checked again by mayTransmit.
 if(t.identityState!=4||(!t.smallBatch.index&&(now<refreshed||now-refreshed>50000000ULL))){
  t.result=ItlIbss::DataTrialResult::NoWindow;t.endRequested=true;awdlIbssDataPublish();return;
 }
 t.phase=P::Submitting;
 if(!awdlIbssPeerSubmit()){t.result=ItlIbss::DataTrialResult::SubmitFailed;t.endRequested=true;}
 else {t.phase=P::Submitted;t.sentNs=itlAwdlRocNowNs();auto p=itlIbssPeerPort;t.queue=p->reserved.queue;t.slot=(unsigned(com.txq[t.queue].cur)+255)%256;
  if(!t.beginReply(awdlProbeCounters->ibss.window.actualOpen,awdlIbssExclusive.deadline))t.endRequested=true;
 }
 awdlIbssDataPublish();
}
