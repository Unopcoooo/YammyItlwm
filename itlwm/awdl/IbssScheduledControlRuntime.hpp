// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// One manual trial owns both rings. Reuse the existing completion/DMA stop
// machinery; never stack a second outstanding management descriptor.
void ItlIwm::awdlIbssScheduledTick(){
 auto &t=itlIbssDataTrial;auto &s=awdlIbssExclusive;auto &schedule=t.controlSchedule;
 using P=ItlIbss::DataTrialPhase;using D=ItlIbss::ExperimentObservation;
 if(!getMainWorkLoop()->inGate()||getMainWorkLoop()->onThread()||!awdlIbssDataActive()||
    !(t.controlMode&1)||t.passive()||t.identityState!=4)return;
 const auto now=itlAwdlRocNowNs();const auto gen=uint32_t(com.sc_generation);
 if(t.cancelled||t.endRequested||s.resetRequested||dmaDetaching||!s.ownsRadio||
    gen!=t.generation||now>=s.deadline){schedule.stop();return;}
 // The quiet tail intentionally stops new advertisements without failing the trial.
 if(schedule.stopped)return;
 if(t.phase!=P::Receiving&&t.phase!=P::Submitted)return; // don't steal the guarded data window
 auto fail=[&](unsigned reason){schedule.stop();t.controlBlocked=reason;t.block=10;
  t.result=ItlIbss::DataTrialResult::PrepareFailed;t.endRequested=true;};
 if(schedule.pending){
  auto p=awdlIbssTxPort;if(!p){fail(1);return;}
  if(p->calls||p->adapter.pending())return;
  if(!schedule.complete(awdlIbssManagementDrained())){fail(2);return;}
 }
 const auto before=itlAwdlRocNowNs();if(!iwm_nic_lock(&com))return;
 const auto gp2=iwm_read_prph(&com,IWM_DEVICE_SYSTEM_TIME_REG);iwm_nic_unlock(&com);const auto after=itlAwdlRocNowNs();
 if(after<before||after-before>2000000ULL)return;
 ItlAwdl::FixedControlView selected;
 if(itlIbssFixedOwner!=this||!itlIbssFixedControl.view(gen,gp2,after,selected)||
    memcmp(selected.source,t.request.peer,6)){t.controlBlocked=3;return;}
 if(!schedule.armed&&!schedule.arm(gen,after,s.deadline,selected.down)){fail(4);return;}
 const auto kind=schedule.due(gen,after,true);
 if(kind==ItlAwdl::BoundedControlSchedule::None){if(schedule.stopped)fail(5);return;}
 if(awdlIbssTxPort){
  if(!awdlIbssManagementDrained())return;
  awdlIbssTxDispose();if(awdlIbssTxPort){fail(6);return;}
 }
 // Preserve the same full-period residency promise as the initial advertisement.
 if(s.deadline-after<ItlAwdl::kSelfControlHorizonNs){schedule.stop();return;}
 ItlAwdl::SelfControlInput input;memcpy(input.source,t.local,6);input.subtype=uint8_t(kind);
 input.sequence=uint16_t((schedule.submitted+2)&4095);input.generation=input.currentGeneration=gen;
 input.epochGp2=input.nowGp2=gp2;input.epochHostNs=before+(after-before)/2;input.nowHostNs=after;input.leaseEndNs=s.deadline;
 uint8_t frame[ItlAwdl::kLinkIdentityLength];size_t length=0;ItlAwdl::FixedControlView proof;
 if(!ItlAwdl::buildFollowingPeriodicControl(input,itlIbssFixedControl,frame,sizeof(frame),length,proof)){fail(7);return;}
 auto p=new ItlIbssTxPort(this);if(!p){fail(8);return;}
 auto &d=awdlProbeCounters->ibss;
 const D::Metric metrics[]={D::TxAccepted,D::TxSubmitted,D::TxCompleted,D::TxReclaimed,D::TxStopReclaimed,
     D::TxDeadlineExpired,D::TxMismatch,D::TxCancelled,D::TxFlushRequested,D::TxFlushFailed,D::TxStatus};
 for(auto metric:metrics)d.set(metric,0);
 p->record("AWDLIbssTxVersion",4);p->record("AWDLIbssTxGeneration",gen);
 t.controlSending=true;t.controlGp2=gp2;t.controlProof=proof;awdlIbssTxPort=p;++p->calls;
 const bool sent=p->adapter.start(frame,length);--p->calls;t.controlSending=false;
 // The pointer stays owned through failed/partial submission until existing
 // cancellation or physical-stop proof can dispose it.
 if(!sent||!schedule.publish(kind,gen,after,proof.down)){fail(9);return;}
 t.controlBlocked=0;itlIbssFixedAdvertised=proof;
 const auto promise=after+ItlAwdl::kSelfControlHorizonNs;if(t.identityMinEndNs<promise)t.identityMinEndNs=promise;
 awdlIbssExperimentEvent(55,uint32_t(kind),0,schedule.submitted,gp2);
}
