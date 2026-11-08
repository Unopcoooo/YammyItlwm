// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "SingleDataProbe.hpp"
#include "IbssWindowProbe.hpp"
#include "LinkIdentityAdvertisement.hpp"
#include "RawRxMetadata.hpp"
#include "TargetRxObserver.hpp"
#include "BoundedControlSchedule.hpp"
#include "ObservedPeerCapabilities.hpp"
#include "ListenerPairObserver.hpp"
#include "EchoComparison.hpp"
#include "RxIngressDiagnostics.hpp"
#include "SmallDataBatch.hpp"
namespace ItlIbss {
enum class DataTrialPhase:uint32_t {Unused,WaitingSta,Receiving,WaitingWindow,Submitting,Submitted,Recovering,Complete,Failed,Cancelled};
enum class DataTrialResult:uint32_t {None,AckReported,NoAck,NoWindow,PrepareFailed,SubmitFailed,TimedOut,Cancelled,RecoveryFailed,PassiveComplete,NativeConfigured};
enum RxCoverage {RxPackets,RxInvalidDescriptor,RxInvalidLength,RxInvalidFrameControl,RxDataWithoutAddresses,
 RxDataWithAddresses,RxLocalReceiver,RxPeerTransmitter,RxPeerToLocal,RxPeerToOtherReceiver,
 RxPeerOnChannel6,RxPeerOnOtherChannel,RxPeerOnUnknownChannel,RxCoverageCount};
struct DataTrial {
 bool nativeQueueMode=false,nativeAttempted=false,nativeStarted=false,nativeDeadlineReached=false,nativeTeardownComplete=false;
 uint32_t nativeError=0;
 uint32_t nativeLeaseReject=0,nativeDeferrals=0;
 uint32_t nativeTxTicks=0,nativeQueuedTicks=0,nativeWindowDenied=0,nativeTaken=0,nativeFrameStarted=0,nativeFrameCompleted=0;
 uint64_t nativeStartNs=0,nativeStopNs=0,nativeFirstDeferredNs=0;
 ItlAwdl::SmallDataBatch smallBatch;
 ItlAwdl::SingleDataRequest currentRequest()const{
  auto r=request;if(smallBatch.enabled)r.token=ItlAwdl::SmallDataBatch::tokenAt(request.token,smallBatch.index);return r;
 }
 RxIngressDiagnostics ingress;
 ItlAwdl::ListenerPairObserver listenerPair;
 ItlAwdl::EchoComparison echo;
 unsigned controlMode=0; // 0 legacy, 1 schedule, 2 observed HT peer, 3 both; explicit manual entry only.
 ItlAwdl::BoundedControlSchedule controlSchedule;
 ItlAwdl::ObservedPeerCapabilities peerCapabilities;
 ItlAwdl::FixedControlView controlProof;
 bool controlSending=false;uint32_t controlGp2=0,controlBlocked=0,peerCapabilitiesApplied=0;
 uint32_t peerRxHt=0,peerRxOtherRate=0,peerRxLastRate=0;
 ItlAwdl::SingleDataRequest request;DataTrialPhase phase=DataTrialPhase::Unused;DataTrialResult result=DataTrialResult::None;
 bool listenerTx=false;unsigned listenerTxStage=0,listenerTxCompletions=0;uint32_t listenerTxStatus=0;uint64_t listenerTxLastNs=0;
 bool passiveIbss=false;
 ItlAwdl::TargetRxObserver targetRx;
 bool passive()const{return listener||passiveIbss;}
 bool listener=false,listenerQueueReserved=false,listenerConfigured=false,listenerFinished=false;
 bool everArmed=false,cancelled=false,prepared=false,endRequested=false,completed=false,ack=false,stopReclaimed=false,recovered=false;
 uint32_t generation=0,rawStatus=UINT32_MAX,block=0,run=0;unsigned queue=0,slot=0;
 uint64_t armedNs=0,baselineLastNs=0,deadlineNs=0,sentNs=0;uint8_t local[6]={};
 ItlAwdl::SingleDataProbe replyObserver;
 uint64_t replyEndNs=0,listenerStartNs=0,listenerObserveNs=0;
 uint32_t rawDataFrames=0,commonDataFrames=0,peerDataFrames=0,listenerPeerCommonData=0;
 uint32_t peerRxStages[ItlAwdl::RawStageCount]={},replyPeerRxStages[ItlAwdl::RawStageCount]={};
 uint32_t rxCoverage[RxCoverageCount]={},replyRxCoverage[RxCoverageCount]={};
 bool inReplyWindow(uint64_t now)const{
  return phase==DataTrialPhase::Submitted&&!cancelled&&!endRequested&&sentNs&&now>=sentNs&&now<replyEndNs;
 }
 // Raw ingress only: coverage does not depend on channel/address matching or trace-ring capacity.
 void observeRxCoverage(const ItlAwdl::RawRxMetadata &m,uint64_t now){
  const bool reply=inReplyWindow(now);
  auto count=[&](RxCoverage k){if(rxCoverage[k]<UINT32_MAX)++rxCoverage[k];if(reply&&replyRxCoverage[k]<UINT32_MAX)++replyRxCoverage[k];};
  count(RxPackets);
  if(!m.descriptorValid){count(RxInvalidDescriptor);return;}
  if(!m.lengthValid){count(RxInvalidLength);return;}
  if(!m.fcValid){count(RxInvalidFrameControl);return;}
  if((m.frameControl&0x0c)!=8)return;
  if(!m.addressValid){count(RxDataWithoutAddresses);return;}
  count(RxDataWithAddresses);const bool toLocal=!memcmp(m.receiver,local,6);
  if(toLocal)count(RxLocalReceiver);
  if(memcmp(m.transmitter,request.peer,6))return;
  if(m.rateValid&&m.statusValid&&(m.status&3)==3){
   peerRxLastRate=m.rateFlags;auto &value=(m.rateFlags&(1u<<8))?peerRxHt:peerRxOtherRate;
   if(value<UINT32_MAX)++value;
  }
  count(RxPeerTransmitter);count(toLocal?RxPeerToLocal:RxPeerToOtherReceiver);
  count(m.channel==6?RxPeerOnChannel6:(m.channel?RxPeerOnOtherChannel:RxPeerOnUnknownChannel));
 }
 void observePeerRxStage(unsigned stage,const ItlAwdl::RawRxMetadata &m,uint32_t gen,uint64_t now){
  if(!active()||gen!=generation||stage>=ItlAwdl::RawStageCount)return;
  if(stage==ItlAwdl::RawIngress)observeRxCoverage(m,now);
  if(passive()&&stage==ItlAwdl::CommonFrame&&m.channel==6&&m.addressValid&&m.fcValid&&
     (m.frameControl&0x0c)==8&&!memcmp(m.transmitter,request.peer,6)&&listenerPeerCommonData<UINT32_MAX)++listenerPeerCommonData;
  if(m.channel!=6||!m.addressValid||memcmp(m.receiver,local,6)||memcmp(m.transmitter,request.peer,6))return;
  if(peerRxStages[stage]<UINT32_MAX)++peerRxStages[stage];
  if(inReplyWindow(now)&&replyPeerRxStages[stage]<UINT32_MAX)++replyPeerRxStages[stage];
 }
 void observeTargetFrame(const uint8_t *p,size_t n,uint32_t gen,unsigned channel){
  if(active()&&generation==gen&&channel==6)targetRx.observe(p,n,local,request.peer,request.peerIp,request.token,listener);
 }
 void observeRawData(bool valid,uint32_t fc){if(valid&&(fc&0x0c)==8&&rawDataFrames<UINT32_MAX)++rawDataFrames;}
 void observeCommonData(const uint8_t *wire,size_t size){
  if(!wire||size<24||(wire[0]&0x0c)!=8)return;
  if(commonDataFrames<UINT32_MAX)++commonDataFrames;
  if(!memcmp(wire+4,local,6)&&!memcmp(wire+10,request.peer,6)&&peerDataFrames<UINT32_MAX)++peerDataFrames;
 }
 unsigned identityState=0;uint32_t identityTxGp2=0;
 uint64_t identityEpochNs=0,identityMinEndNs=0;uint32_t identityEpochGp2=0;
 // The fixed local advertisement uses a 16 TU AW period. Do not call a
 // back-to-back frame a new AW refresh: its AW/down fields are unchanged.
 uint64_t identityRefreshNotBeforeNs()const{
  return identityEpochNs+uint64_t(uint32_t(identityTxGp2-identityEpochGp2)+16*1024)*1000;
 }
 bool mayReplanLateClose(const WindowProbe &w)const{
  return phase==DataTrialPhase::WaitingWindow&&prepared&&identityState==2&&!cancelled&&!endRequested&&
   !sentNs&&!completed&&!stopReclaimed&&result==DataTrialResult::None&&generation==w.plan.generation&&w.lateCloseCancelled();
 }
 bool beginReply(uint32_t gp2,uint64_t lease){
  if(!sentNs||lease<=sentNs)return false;
  const auto current=currentRequest();
  replyObserver={};replyObserver.request=current;replyObserver.generation=generation;
  memcpy(replyObserver.local,local,6);size_t size=0;
  if(echo.profile){if(!echo.prepare(request,local,generation))return false;}
  else if(!ItlAwdl::buildSingleDataEcho(current,local,generation,uint16_t(smallBatch.index+1),replyObserver.frame,sizeof(replyObserver.frame),size))return false;
  replyObserver.submitted=1;replyObserver.sentNs=sentNs;replyObserver.sentGp2=gp2;
  replyEndNs=lease-sentNs>2500000000ULL?sentNs+2500000000ULL:lease;
  if(smallBatch.enabled){
   if(!smallBatch.endNs)smallBatch.endNs=replyEndNs;
   replyEndNs=smallBatch.endNs;if(!smallBatch.markSubmitted())return false;
  }
  return true;
 }
 bool observeReply(const uint8_t *wire,size_t size,uint32_t gen,unsigned channel,uint32_t gp2,uint64_t now){
  const bool matched=phase==DataTrialPhase::Submitted&&!cancelled&&!endRequested&&replyEndNs&&now<replyEndNs&&
   (echo.profile?observeComparisonReply(wire,size,gen,channel,gp2,now):replyObserver.reply(wire,size,gen,channel,gp2,now));
  if(matched&&smallBatch.enabled)smallBatch.markReply();return matched;
 }
 bool observeComparisonReply(const uint8_t *wire,size_t size,uint32_t gen,unsigned channel,uint32_t gp2,uint64_t now){
  if(replyObserver.replyObserved||gen!=generation||channel!=6||now<sentNs||uint32_t(gp2-replyObserver.sentGp2)>5000000||!echo.reply(wire,size,request,local))return false;
  replyObserver.replyObserved=true;replyObserver.replyGp2=gp2;return true;
 }
 bool active()const{return phase>=DataTrialPhase::WaitingSta&&phase<=DataTrialPhase::Recovering;}
 bool mayRepeat(uint64_t token)const{
  return everArmed&&run&&run<UINT32_MAX&&recovered&&!cancelled&&
   (phase==DataTrialPhase::Complete||phase==DataTrialPhase::Failed)&&
   result!=DataTrialResult::RecoveryFailed&&token&&token!=request.token;
 }
 // Metadata eligibility only; admission must separately prove native drain and radio recovery.
 bool mayRestartCancelledNative(uint64_t token)const{
  return nativeQueueMode&&everArmed&&run&&run<UINT32_MAX&&cancelled&&endRequested&&
   phase==DataTrialPhase::Cancelled&&result==DataTrialResult::Cancelled&&
   nativeTeardownComplete&&!nativeError&&token&&token!=request.token;
 }
 bool arm(const ItlAwdl::SingleDataRequest &r,const uint8_t source[6],uint32_t gen,uint64_t now){
  uint8_t frame[104];size_t n=0;
  if(everArmed||!now||now>UINT64_MAX-240000000000ULL||!ItlAwdl::buildSingleDataEcho(r,source,gen,0,frame,sizeof(frame),n))return false;
  request=r;memcpy(local,source,6);everArmed=true;armedNs=baselineLastNs=now;deadlineNs=now+90000000000ULL;phase=DataTrialPhase::WaitingSta;return true;
 }
 bool peerFresh(const ItlAwdl::DataContext &c)const{
  if(!c.gateHeld||!c.generation||c.generation!=generation||!c.peers||c.peers->count>ItlAwdl::kMaxPeers||
     !c.modelEpoch||c.modelGeneration!=generation||c.nowNs<c.modelHostNs||c.nowNs-c.modelHostNs>500000000ULL)return false;
  for(unsigned i=0;i<c.peers->count;++i){const auto &p=c.peers->peers[i];
   if(memcmp(p.address,request.peer,6))continue;
   return p.diagnosticId&&p.hasSync&&p.hasChannels&&p.timing.consistent&&p.timing.streak>=3&&
    p.timing.receivedGp2==p.syncReceivedGp2&&c.gp2-p.lastSeenGp2<=500000&&c.gp2-p.syncReceivedGp2<=500000;
  }
  return false;
 }
 // Exclusive IBSS residency is checked by the native transport. AUX association
 // or START flags must not be fabricated to reuse an AUX window predicate.
 bool mayTransmit(const ItlAwdl::DataContext &c,const WindowProbe &w)const{
  if(phase!=DataTrialPhase::Submitting||cancelled||endRequested||!prepared||!peerFresh(c)||
     c.channel!=6||c.deadlineNs<=c.nowNs||c.deadlineNs-c.nowNs<5000000ULL||
     !w.inside(c.generation,c.gp2)||w.plan.epoch!=c.modelEpoch||windowDelta(w.plan.close,c.gp2)<5000)return false;
  for(unsigned i=0;i<c.peers->count;++i){const auto &p=c.peers->peers[i];if(memcmp(p.address,request.peer,6))continue;
   if(p.diagnosticId!=w.plan.peer||!p.sync.awPeriod||p.sync.awPeriod>64||!p.sync.presenceMode||p.sync.presenceMode>4||
      uint32_t(p.sync.awPeriod)*p.sync.presenceMode>64||(p.channels.encoding!=1&&p.channels.encoding!=3))return false;
   ItlAwdl::WindowPrediction prediction;ItlAwdl::SlotView slot;
   if(!ItlAwdl::predictWindowWithTxDelay(p.sync,p.syncReceivedGp2,c.gp2,500000,p.timing.txDelayUs,prediction))return false;
   const uint32_t period=uint32_t(p.sync.awPeriod)*p.sync.presenceMode*1024;
   prediction.nextBoundarySequence=uint16_t(prediction.nextBoundarySequence-p.sync.presenceMode);
   const uint32_t boundary=prediction.nextBoundaryGp2-period;
   const auto residual=windowDelta(boundary,w.plan.boundary);
   // TU-quantized updates may move the estimate; transmit only inside both guarded windows.
   return period==w.plan.period&&residual>=-2048&&residual<=2048&&
    prediction.nextBoundarySequence==w.plan.boundarySequence&&
    windowDelta(c.gp2,boundary+2048)>=0&&windowDelta(boundary+period-2048,c.gp2)>=5000&&
    ItlAwdl::predictNextSlot(p.sync,p.channels,prediction,slot)&&slot.slot==w.plan.selectedSlot&&slot.channel==6&&
    p.channels.entries[slot.slot].auxiliary==(p.channels.encoding==1?0x2b:0x51);
  }
  return false;
 }
 void finishRecovery(bool stable){
  if(nativeQueueMode){recovered=stable;if(!stable)result=DataTrialResult::RecoveryFailed;
   phase=cancelled?DataTrialPhase::Cancelled:(stable&&nativeStarted&&nativeTeardownComplete&&!nativeError&&result==DataTrialResult::NativeConfigured?DataTrialPhase::Complete:DataTrialPhase::Failed);return;}
  recovered=stable;phase=cancelled?DataTrialPhase::Cancelled:(stable&&(passive()?(listenerConfigured&&listenerFinished&&result==DataTrialResult::PassiveComplete):(ack&&completed&&!stopReclaimed&&result==DataTrialResult::AckReported&&(!smallBatch.enabled||smallBatch.success())))?DataTrialPhase::Complete:DataTrialPhase::Failed);if(!stable)result=DataTrialResult::RecoveryFailed;
 }
};
// A never-started request has no retired firmware image to prove stopped.
template<class State> bool untouchedDataTrialReusable(const State &s){
 return s.phase==decltype(s.phase)::Idle&&!s.ownsRadio&&!s.resetRequested&&!s.operation&&
  !s.ordinary.active()&&s.commandAuthority.idle()&&!s.primaryError&&!s.recoveryError&&
  !s.session.resources.valid()&&!s.session.commandInFlight&&!s.session.needsReset;
}
// Historical stop proof concerns the retired exclusive image; ordinary STA DMA may now run.
template<class State> bool recoveredDataTrialReusable(const State &s){
 return s.phase==decltype(s.phase)::Associated&&s.resetProven&&!s.ownsRadio&&!s.resetRequested&&
  !s.operation&&!s.ordinary.active()&&s.commandAuthority.idle()&&!s.primaryError&&!s.recoveryError&&
  !s.session.resources.valid()&&!s.session.commandInFlight&&!s.session.needsReset;
}
// Only ordinary association timeout after proven exclusive-image retirement.
template<class State> bool lateRecoveryRetired(const State &s){
 return s.phase==decltype(s.phase)::Failed&&s.primaryError==ETIMEDOUT&&!s.recoveryError&&
  s.resetProven&&!s.ownsRadio&&!s.resetRequested&&!s.operation&&!s.ordinary.active()&&
  s.commandAuthority.idle()&&!s.session.resources.valid()&&!s.session.commandInFlight&&!s.session.needsReset;
}
}
