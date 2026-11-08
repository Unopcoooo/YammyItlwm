// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "NativeLinkAdvertisement.hpp"
// Uses the retained search worker and the existing single-descriptor owner.
void ItlIwm::awdlNativeControlTxPublish(){
 if(!getMainWorkLoop()->inGate()||!getController())return;
 const auto &t=awdlNativeControlTx;auto d=OSDictionary::withCapacity(17);if(!d)return;
 const char *keys[]={"Version","Epoch","Generation","Phase","Reason","Submitted","Completed","Outstanding","RoleAccepted","LeaseEndNs","Mode","PSF","MIF","SchedulePending","RoleTxConfirmed","DrainedAtSearchStop","StopBlockers"};
 const uint64_t values[]={1,t.epoch,t.generation,t.phase,t.reason,t.submitted,t.completed,t.port&&t.port==awdlIbssTxPort,awdlNativeSyncRoleReady(),t.leaseEndNs,t.periodic?2u:1u,t.periodic?t.schedule.psf:(t.submitted?1u:0u),t.periodic?t.schedule.mif:(t.submitted>=2?1u:0u),t.periodic&&t.schedule.pending,awdlNativeSyncRoleConfirmed(),t.drainedAtSearchStop,t.stopBlockers};
 bool ok=true;for(unsigned n=0;n<17&&ok;++n){auto v=OSNumber::withNumber(values[n],64);if(!v){ok=false;break;}ok=d->setObject(keys[n],v);v->release();}
 if(ok)getController()->setProperty("AWDLNativeControlTx",d);d->release();
}
IOReturn ItlIwm::awdlNativeControlTxArm(uint64_t epoch,bool periodic){
 using T=ItlAwdl::NativeSearchControlTx;auto &t=awdlNativeControlTx;
 if(!getMainWorkLoop()->inGate()||dmaDetaching||!epoch||!awdlNativeSearch.matches(epoch)||
    t.phase!=T::Idle||!awdlNativeSearchEnabled()||awdlIbssTxPort||awdlIbssPeerActive()||
    awdlIbssDataActive()||ItlAwdl::gDataPort.nativeSession||ItlIbssTxPort::radioRejection(this,true))return kIOReturnNotReady;
 const auto now=itlAwdlRocNowNs();const auto end=awdlNativeSearch.deadlineNs;
 if(end>awdlIbssExclusive.deadline||end<=now||end-now<5000000000ULL)return kIOReturnNotReady;
 t.epoch=epoch;t.generation=awdlNativeSearch.generation;t.leaseEndNs=end;t.radioLeaseEndNs=awdlIbssExclusive.deadline;t.phase=T::Armed;t.periodic=periodic;
 awdlNativeControlTxPublish();getMainCommandGate()->commandWakeup(&awdlIbssWorkPending,false);return kIOReturnSuccess;
}
void ItlIwm::awdlNativeControlTxStop(unsigned reason){
 using T=ItlAwdl::NativeSearchControlTx;auto &t=awdlNativeControlTx;
 if(!getMainWorkLoop()->inGate()||!t.active())return;
 const auto &search=awdlNativeSearch;
 // Keep the cause of an ineligible cancellation available after the worker exits.
 uint32_t blockers=0;
 if(reason!=1 || t.phase!=T::Armed || !t.periodic || !t.nativeRoleAccepted)blockers|=1;
 if(!search.matches(t.epoch) || search.failure || !(search.stopRequested||search.expired))blockers|=2;
 if(t.generation!=search.generation || t.generation!=uint32_t(com.sc_generation))blockers|=4;
 if(t.leaseEndNs!=search.deadlineNs || t.radioLeaseEndNs!=awdlIbssExclusive.deadline)blockers|=8;
 if(t.port || awdlIbssTxPort || t.schedule.pending)blockers|=16;
 if(!t.schedule.psf || !t.schedule.mif || t.submitted!=t.completed ||
    t.schedule.submitted!=t.submitted || t.schedule.completed!=t.completed)blockers|=32;
 t.stopBlockers=blockers;
 t.drainedAtSearchStop=blockers==0;
 awdlNativeDiscoveryStop();
 t.phase=reason==1?T::Cancelled:T::Failed;t.reason=reason;t.schedule.stop();
 // Mark terminal before a flush can sleep/reenter. Only the recorded owner is touched.
 if(t.port&&t.port==awdlIbssTxPort)awdlIbssTxCancel();
 awdlNativeControlTxPublish();
}
void ItlIwm::awdlNativeControlTxTick(){
 using T=ItlAwdl::NativeSearchControlTx;using D=ItlIbss::ExperimentObservation;
 auto &t=awdlNativeControlTx;if(!getMainWorkLoop()->inGate()||getMainWorkLoop()->onThread())return;
 if(!t.active()){
  const bool cleared=t.port&&!awdlIbssTxPort;if(cleared)t.port=nullptr;
  if(cleared||t.nativeRoleAccepted)awdlNativeControlTxPublish();return;
 }
 if(t.nativeRoleAccepted&&!awdlNativeSyncRoleReady()){awdlNativeControlTxStop(1);return;}
 auto fail=[&](unsigned reason){awdlNativeControlTxStop(reason);awdlNativeSearch.fail(t.epoch,EIO);};
 if(!awdlNativeSearch.matches(t.epoch)||!awdlNativeSearchEnabled()||
    t.generation!=uint32_t(com.sc_generation)||t.leaseEndNs!=awdlNativeSearch.deadlineNs||t.radioLeaseEndNs!=awdlIbssExclusive.deadline){awdlNativeControlTxStop(1);return;}
 if(t.phase==T::Pending){
  auto p=awdlIbssTxPort;if(!p||p!=t.port){fail(2);return;}
  const auto &d=awdlProbeCounters->ibss;
  if(d.get(D::TxDeadlineExpired)||d.get(D::TxMismatch)||d.get(D::TxFlushFailed)){fail(12);return;}
  if(p->calls||p->adapter.pending())return;
  if(!awdlIbssManagementDrained()){fail(3);return;}
  if(t.periodic&&!t.schedule.complete(true)){fail(13);return;}
  ++t.completed;
  if(t.nativeRoleAccepted){
   const auto done=itlAwdlRocNowNs();const auto remaining=t.leaseEndNs>done?t.leaseEndNs-done:0;
   t.roleQuietUntilNs=done+(remaining<ItlAwdl::kSelfControlHorizonNs?remaining:ItlAwdl::kSelfControlHorizonNs);
  }
  awdlIbssTxDispose();
  if(awdlIbssTxPort){fail(4);return;}t.port=nullptr;
  if(!t.periodic&&t.completed==2){t.schedule.stop();t.phase=T::Complete;awdlNativeControlTxPublish();return;}
  t.phase=T::Armed;awdlNativeControlTxPublish();
 }
 if(awdlNativeDiscovery.peerFlight)return;
 if(awdlIbssTxPort&&awdlIbssTxPort==awdlNativeDiscovery.port)return;
 if(awdlIbssTxPort||(!t.periodic&&t.submitted>=2)){fail(5);return;}
 const auto before=itlAwdlRocNowNs();
 if(t.leaseEndNs<=before||t.leaseEndNs-before<ItlAwdl::kSelfControlHorizonNs+2000000000ULL){
  if(t.periodic&&t.completed&&t.completed==t.submitted){t.schedule.stop();t.phase=T::Complete;awdlNativeControlTxPublish();}
  else fail(6);return;
 }
 if(!iwm_nic_lock(&com)){fail(7);return;}
 const auto gp2=iwm_read_prph(&com,IWM_DEVICE_SYSTEM_TIME_REG);iwm_nic_unlock(&com);const auto after=itlAwdlRocNowNs();
 if(after<before||after-before>2000000ULL){fail(8);return;}
 if(!t.submitted){t.epochGp2=gp2;t.epochHostNs=before+(after-before)/2;}
 ItlAwdl::SelfControlInput input;input.receiveValidation=awdlNativeSearch.receiveValidation();memcpy(input.source,awdlNativeSearch.local,6);
 if(t.periodic)input.advertisementPeriod=t.schedule.psfPeriodTu;
 input.subtype=t.periodic?0:(t.submitted?3:0);input.sequence=uint16_t(t.submitted);
 input.generation=t.generation;input.currentGeneration=uint32_t(com.sc_generation);
 input.epochGp2=t.epochGp2;input.nowGp2=gp2;input.epochHostNs=t.epochHostNs;input.nowHostNs=after;input.leaseEndNs=t.leaseEndNs;
 uint8_t frame[512]={},local[6]={};size_t length=0;ItlAwdl::SelfControlEvidence proof;
 if(!ItlAwdl::buildSelfControlAdvertisement(input,frame,sizeof(frame),length,proof)||
    !awdlIbssLocalAddress(local)||memcmp(local,input.source,6)||
    memcmp(frame+16,awdlIbssExclusive.session.mac.bssid,6)){fail(9);return;}
 auto kind=ItlAwdl::BoundedControlSchedule::Psf;
 if(t.periodic){
  if(!t.schedule.armed&&!t.schedule.arm(t.generation,after,t.leaseEndNs,proof.downCounterTu,awdlNativeSearch.receiveValidation())){fail(14);return;}
  kind=t.schedule.due(t.generation,after,true);
  if(kind==ItlAwdl::BoundedControlSchedule::None){if(t.schedule.stopped)fail(15);return;}
  input.subtype=uint8_t(kind);
  if(kind==ItlAwdl::BoundedControlSchedule::Mif&&
     !ItlAwdl::buildSelfControlAdvertisement(input,frame,sizeof(frame),length,proof)){fail(9);return;}
 }
 if(!ItlAwdl::appendNativeLinkAdvertisement(frame,sizeof(frame),length)){fail(9);return;}
 t.sampleHostNs=before+(after-before)/2;
 auto p=new ItlIbssTxPort(this);if(!p){fail(10);return;}
 if(t.phase!=T::Armed||awdlIbssTxPort||!awdlNativeSearchEnabled()||t.generation!=uint32_t(com.sc_generation)){
  delete p;awdlNativeControlTxStop(1);return;
 }
 auto &d=awdlProbeCounters->ibss;
 const D::Metric metrics[]={D::TxAccepted,D::TxSubmitted,D::TxCompleted,D::TxReclaimed,D::TxStopReclaimed,
  D::TxDeadlineExpired,D::TxMismatch,D::TxCancelled,D::TxFlushRequested,D::TxFlushFailed,D::TxStatus};
 for(auto metric:metrics)d.set(metric,0);
 p->record("AWDLIbssTxVersion",5);p->record("AWDLIbssTxGeneration",t.generation);
 // The bounded registry counters cover every frame; retain full raw bytes only for the first PSF/MIF.
 if(!t.periodic||!t.submitted||(kind==ItlAwdl::BoundedControlSchedule::Mif&&!t.schedule.mif))
  ItlIbss::appendControlTxEvidence(d,input,frame,uint32_t(t.epoch));
 if(t.periodic&&!t.schedule.publish(kind,t.generation,after,proof.downCounterTu)){delete p;fail(16);return;}
 t.port=p;t.phase=T::Pending;awdlIbssTxPort=p;++p->calls;
 const size_t baseLength=length;
 auto &discovery=awdlNativeDiscovery;
 if(t.periodic && kind==ItlAwdl::BoundedControlSchedule::Mif && discovery.open && !discovery.closed &&
    discovery.epoch==t.epoch && discovery.generation==t.generation)
  length=discovery.mifService.take(frame,length,sizeof(frame),t.epoch,t.generation,itlAwdlRocNowNs());
 const bool sent=p->adapter.start(frame,length);
 if(sent && length>baseLength)++discovery.mifService.sent;--p->calls;
 if(p->retiring)awdlIbssTxDispose();
 if(sent)++t.submitted;
 if(t.phase!=T::Pending){awdlNativeControlTxPublish();return;}
 if(!sent){fail(11);return;}
 awdlNativeControlTxPublish();
}
