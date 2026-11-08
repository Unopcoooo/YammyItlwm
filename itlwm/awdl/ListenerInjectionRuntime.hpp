#pragma once
#include "ListenerTxEvidence.hpp"
void ItlIwm::awdlListenerTxResponse(unsigned queue,unsigned slot,const void *wire,size_t length){
 auto p=itlListenerTxPort;const auto &t=itlIbssDataTrial;
 if(!getMainWorkLoop()->inGate()||!awdlProbeCounters||!awdlListenerTxActive()||
    !p||p->hal!=this||!p->adapter.pending()||!p->matching(slot)||
    t.generation!=uint32_t(com.sc_generation))return;
 using H=ItlIbss::ExperimentRows;H::Row row;
 if(!ItlIbss::listenerTxResponseRow(wire,length,t.listenerTxStage,queue,slot,row))return;
 const auto us=itlAwdlRocNowNs()/1000;
 row.v[H::HostUsLow]=uint32_t(us);row.v[H::HostUsHigh]=uint32_t(us>>32);
 row.v[H::Generation]=t.generation;row.v[H::Phase]=unsigned(awdlIbssExclusive.phase);
 awdlProbeCounters->ibss.control.append(row);
}
bool ItlIwm::awdlListenerTxActive(){return awdlListenerActive()&&itlIbssDataTrial.listenerTx&&itlIbssDataTrial.listenerQueueReserved;}
IOReturn ItlIwm::awdlListenerTxRequest(const ItlAwdl::SingleDataRequest &input){
 if(!getMainWorkLoop()->inGate()||itlListenerTxPort)return kIOReturnNotReady;
 const auto result=awdlListenerRequest(input);
 if(result==kIOReturnSuccess){itlIbssDataTrial.listenerTx=true;awdlIbssDataPublish();}
 return result;
}
bool ItlIwm::awdlListenerTxWindowValid(){
 auto &t=itlIbssDataTrial;auto &s=awdlIbssExclusive;
 if(!awdlListenerTxActive()||!getMainWorkLoop()->inGate()||getMainWorkLoop()->onThread()||
    t.cancelled||t.endRequested||!s.ownsRadio||s.resetRequested||s.operation||s.session.needsReset||
    t.generation!=uint32_t(com.sc_generation)||itlIbssFixedOwner!=this)return false;
 const auto before=itlAwdlRocNowNs();if(!iwm_nic_lock(&com))return false;
 const auto gp2=iwm_read_prph(&com,IWM_DEVICE_SYSTEM_TIME_REG);iwm_nic_unlock(&com);const auto now=itlAwdlRocNowNs();
 if(now<before||now-before>2000000ULL||now>=s.deadline||s.deadline-now<ItlAwdl::kSelfControlHorizonNs)return false;
 ItlAwdl::DataContext c;awdlProbeCounters->dataModelView(c);c.gateHeld=true;c.generation=t.generation;c.gp2=gp2;c.nowNs=now;
 if(!t.peerFresh(c))return false;
 ItlAwdl::FixedControlView selected;if(!itlIbssFixedControl.view(t.generation,gp2,now,selected)||memcmp(selected.source,t.request.peer,6))return false;
 if(itlListenerTxPort&&t.listenerTxStage<2){
  const auto &a=t.controlProof.election;const auto &b=selected.election;
  if(t.controlProof.generation!=t.generation||memcmp(a.masterAddress,b.masterAddress,6)||memcmp(a.syncAddress,b.syncAddress,6)||
     a.height!=b.height||a.masterMetric!=b.masterMetric||a.masterCounter!=b.masterCounter)return false;
 }
 if(t.listenerTxStage==2&&!itlIbssFixedDataAllowed(this,t.generation,gp2,now))return false;
 for(unsigned i=0;i<c.peers->count;++i){const auto &p=c.peers->peers[i];if(memcmp(p.address,t.request.peer,6))continue;
  ItlAwdl::WindowPrediction w;ItlAwdl::SlotView slot;
  if(!ItlAwdl::predictWindowWithTxDelay(p.sync,p.syncReceivedGp2,gp2,500000,p.timing.txDelayUs,w))return false;
  const auto period=uint32_t(p.sync.awPeriod)*p.sync.presenceMode*1024;
  if(!period||period>65536)return false;
  const auto boundary=w.nextBoundaryGp2-period;w.nextBoundarySequence=uint16_t(w.nextBoundarySequence-p.sync.presenceMode);
  return ItlIbss::windowDelta(gp2,boundary)>=3072&&ItlIbss::windowDelta(boundary+period,gp2)>=5000&&
   ItlAwdl::predictNextSlot(p.sync,p.channels,w,slot)&&slot.channel==6;
 }
 return false;
}
int ItlIwm::awdlListenerTxComplete(unsigned queue,unsigned slot,uint32_t status){
 if(queue!=2)return 0;auto p=itlListenerTxPort;
 if(!p)return awdlListenerTxActive()?1:0;
 if(p->hal!=this||!getMainWorkLoop()->inGate())return 1;
 ++p->calls;
 if(p->adapter.pending()&&!p->matching(slot)){p->mismatch=true;awdlIbssExclusive.resetRequested=true;--p->calls;return 1;}
 const auto result=p->adapter.complete(uint32_t(com.sc_generation),queue,slot);
 using C=ItlIbss::SingleFrameAdapter<ItlListenerTxPort,true>::Completion;
 if(result==C::Reclaim){
  auto &t=itlIbssDataTrial;t.listenerTxStatus=status;++t.listenerTxCompletions;
  if(t.listenerTxStage==3){t.completed=true;t.rawStatus=status;}
  if((status&255)!=1&&(status&255)!=2){t.result=ItlIbss::DataTrialResult::SubmitFailed;t.endRequested=true;}
  awdlIbssExperimentEvent(57,2,0,t.listenerTxStage,status);getMainCommandGate()->commandWakeup(&awdlIbssWorkPending,false);
 }
 --p->calls;return result==C::Reclaim?2:1;
}
void ItlIwm::awdlListenerTxCancel(){auto p=itlListenerTxPort;if(!p||p->hal!=this)return;++p->calls;p->adapter.cancel();--p->calls;}
void ItlIwm::awdlListenerTxStopped(){
 auto p=itlListenerTxPort;if(!p||p->hal!=this)return;++p->calls;p->stopEvidence=true;p->retiring=true;
 p->adapter.hardwareStopped();--p->calls;
 if(!p->calls&&!p->held&&!p->adapter.pending()&&!p->mismatch){itlListenerTxPort=nullptr;delete p;}
}
void ItlIwm::awdlListenerTxTick(){
 auto &t=itlIbssDataTrial;auto &s=awdlIbssExclusive;
 if(!awdlListenerTxActive())return;
 auto p=itlListenerTxPort;
 if(p){++p->calls;p->adapter.tick();--p->calls;if(p->adapter.pending()||p->held||p->calls||p->mismatch)return;
  itlListenerTxPort=nullptr;delete p;
 }
 const auto now=itlAwdlRocNowNs();
 if(t.cancelled||t.endRequested||s.resetRequested||now>=s.deadline){
  t.listenerFinished=t.listenerConfigured&&t.completed&&!t.cancelled&&!s.resetRequested;
  if(t.result==ItlIbss::DataTrialResult::None)t.result=t.listenerFinished?ItlIbss::DataTrialResult::PassiveComplete:ItlIbss::DataTrialResult::NoWindow;
  t.phase=ItlIbss::DataTrialPhase::Recovering;awdlIbssExclusiveEnd();return;
 }
 if(!t.listenerConfigured||t.listenerTxStage>=3||
    (t.listenerTxStage==1&&now-t.listenerTxLastNs<16384000ULL)||!awdlListenerTxWindowValid())return;
 uint8_t frame[ItlAwdl::kLinkIdentityLength];size_t length=0;ItlAwdl::FixedControlView proof;
 if(t.listenerTxStage<2){
  const auto before=itlAwdlRocNowNs();if(!iwm_nic_lock(&com))return;
  const auto gp2=iwm_read_prph(&com,IWM_DEVICE_SYSTEM_TIME_REG);iwm_nic_unlock(&com);const auto after=itlAwdlRocNowNs();
  if(after<before||after-before>2000000ULL)return;
  ItlAwdl::SelfControlInput in;memcpy(in.source,t.local,6);in.subtype=3;in.sequence=uint16_t(t.listenerTxStage);
  in.generation=in.currentGeneration=t.generation;in.epochGp2=in.nowGp2=gp2;
  in.epochHostNs=before+(after-before)/2;in.nowHostNs=after;in.leaseEndNs=s.deadline;
  if(!ItlAwdl::buildFollowingPeriodicControl(in,itlIbssFixedControl,frame,sizeof(frame),length,proof))return;
 }else if(!ItlAwdl::buildSingleDataEcho(t.request,t.local,t.generation,1,frame,sizeof(frame),length))return;
 if(t.listenerTxStage<2)t.controlProof=proof;
 auto port=new ItlListenerTxPort(this);if(!port){t.endRequested=true;t.result=ItlIbss::DataTrialResult::PrepareFailed;return;}
 itlListenerTxPort=port;++port->calls;
 const bool sent=port->adapter.start(frame,length);--port->calls;
 if(port->retiring&&!port->calls&&!port->held&&!port->adapter.pending()&&!port->mismatch){itlListenerTxPort=nullptr;delete port;t.endRequested=true;return;}
 if(!sent){t.endRequested=true;t.result=ItlIbss::DataTrialResult::SubmitFailed;return;}
 if(t.listenerTxStage<2)itlIbssFixedAdvertised=proof;
 ++t.listenerTxStage;t.listenerTxLastNs=itlAwdlRocNowNs();
 if(t.listenerTxStage==3){t.prepared=true;t.phase=ItlIbss::DataTrialPhase::Submitted;t.sentNs=t.listenerTxLastNs;t.queue=2;t.slot=(unsigned(com.txq[2].cur)+255)%256;
  if(!iwm_nic_lock(&com)){t.endRequested=true;return;}const auto gp2=iwm_read_prph(&com,IWM_DEVICE_SYSTEM_TIME_REG);iwm_nic_unlock(&com);
  if(!t.beginReply(gp2,s.deadline))t.endRequested=true;
  else t.replyEndNs=s.deadline-t.sentNs>5000000000ULL?t.sentNs+5000000000ULL:s.deadline;
 }
 awdlIbssExperimentEvent(57,1,0,t.listenerTxStage,uint32_t(length));awdlIbssDataPublish();
}
