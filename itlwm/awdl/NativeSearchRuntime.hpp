// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Bounded receive-residency proof. No peer address, BSD queues or data-ready claim.
bool ItlIwm::awdlNativeSearchReserved(){
 return getMainWorkLoop()&&getMainWorkLoop()->inGate()&&awdlNativeSearch.epoch!=0;
}
bool ItlIwm::awdlNativeSearchMayConfigure(){
 if(!getMainWorkLoop()||!getMainWorkLoop()->inGate())return false;
 if(!awdlNativeSearch.epoch)return true;
 if(dmaDetaching)return false;
 const auto &o=awdlNativeSearch;
 return o.phase==ItlAwdl::NativeSearchOwner::Phase::Starting&&!o.stopRequested&&awdlIbssWorkPending&&
  o.epoch==awdlIbssWorkerEpoch&&itlAwdlRocNowNs()<o.deadlineNs&&ItlAwdl::gDataPort.owner&&
  !ItlAwdl::gDataPort.releasePending&&!memcmp(o.local,ItlAwdl::gDataPort.registeredLocal,6);
}
bool ItlIwm::awdlNativeSearchConsumeRx(){
 if(!getMainWorkLoop()||!getMainWorkLoop()->inGate())return false;
 const auto &o=awdlNativeSearch;
 return o.active()&&o.generation&&o.matches(awdlIbssWorkerEpoch)&&awdlIbssExclusive.ownsRadio&&
  uint32_t(com.sc_generation)==o.generation;
}
bool ItlIwm::awdlNativeSearchAddress(uint8_t out[6]){
 if(!out||!getMainWorkLoop()||!getMainWorkLoop()->inGate()||!awdlNativeSearch.active()||
    !awdlIbssWorkPending||!awdlNativeSearch.matches(awdlIbssWorkerEpoch))return false;
 memcpy(out,awdlNativeSearch.local,6);return true;
}
// An enabled receive backend is distinct from a synchronised peer or BSD data readiness.
bool ItlIwm::awdlNativeSearchEnabled(){
 if(!getMainWorkLoop()||!getMainWorkLoop()->inGate()||dmaDetaching||!awdlProbeCounters)return false;
 const auto &o=awdlNativeSearch;const auto &s=awdlIbssExclusive;
 return !(com.sc_flags&(IWM_FLAG_HW_ERR|IWM_FLAG_RFKILL|IWM_FLAG_SHUTDOWN))&&
  !s.primaryError&&!s.recoveryError&&s.generation==o.generation&&
  o.phase==ItlAwdl::NativeSearchOwner::Phase::Searching&&!o.stopRequested&&!o.expired&&!o.failure&&
  awdlIbssWorkPending&&o.matches(awdlIbssWorkerEpoch)&&o.generation&&uint32_t(com.sc_generation)==o.generation&&
  itlAwdlRocNowNs()<o.deadlineNs&&ItlAwdl::gDataPort.owner&&!ItlAwdl::gDataPort.releasePending&&
  !memcmp(o.local,ItlAwdl::gDataPort.registeredLocal,6)&&s.ownsRadio&&!s.operation&&!s.resetRequested&&
  s.phase==ItlIbss::ExclusivePhase::FirmwareReady&&s.session.resources.valid()&&s.session.quotaConfigured&&
  !s.session.commandInFlight&&!s.session.needsReset;
}
// Same-channel confirmation only; no retune, deadline renewal or shared AUX policy.
bool ItlIwm::awdlNativeSearchMasterChannel(uint32_t &channel){
 if(!awdlNativeSearchEnabled())return false;
 using D=ItlIbss::ExperimentObservation;const auto &d=awdlProbeCounters->ibss;
 const auto generation=awdlNativeSearch.generation;
 ItlIbss::MccChannelProfile m;
 m.decoded=d.get(D::MccProfileVersion)==1&&d.get(D::MccDecoded);m.generation=d.get(D::MccGeneration);
 m.status=d.get(D::MccStatus);m.present=uint8_t(d.get(D::MccPresent));m.flags6=d.get(D::MccFlags6);
 const auto &c=com.sc_ic.ic_channels[6];
 if(awdlIbssExclusive.session.mac.channel!=6||
    !ItlIbss::channel6ProfileAllowed(6,c.ic_freq,c.ic_flags,d.get(D::NvmFlags6),
       d.get(D::NvmChannelFlagsVersion)==1,m,generation))return false;
 channel=6;return true;
}
IOReturn ItlIwm::awdlNativeSearchReadMasterChannel(uint32_t &channel,bool &reserved){
 auto loop=getMainWorkLoop();auto gate=getMainCommandGate();
 if(!loop||!gate)return kIOReturnNotReady;
 ItlAwdlRocLifetime lifetime(this,gate,loop);
 if(!loop->inGate()){
  struct Request{ItlIwm *hal;uint32_t *channel;bool *reserved;}request{this,&channel,&reserved};
  return gate->runAction([](OSObject*,void *arg,void*,void*,void*)->IOReturn{
   auto r=static_cast<Request*>(arg);return r->hal->awdlNativeSearchReadMasterChannel(*r->channel,*r->reserved);
  },&request);
 }
 reserved=awdlNativeSearchReserved();
 return !reserved||awdlNativeSearchMasterChannel(channel)?kIOReturnSuccess:kIOReturnNotReady;
}
IOReturn ItlIwm::awdlNativeSearchEnable(){
 if(!getMainWorkLoop()||!getMainWorkLoop()->inGate()||dmaDetaching||
    (com.sc_flags&(IWM_FLAG_HW_ERR|IWM_FLAG_RFKILL|IWM_FLAG_SHUTDOWN)))return kIOReturnNotReady;
 auto &o=awdlNativeSearch;
 if(awdlNativeControlTx.nativeRoleAccepted&&!awdlNativeSyncRoleReady()){
  // A retired role describes the previous epoch. The request below still checks
  // radio recovery, controller retirement and both DMA owners before reuse.
  if(o.active()||!awdlNativeDiscovery.canRearm(o.epoch))return kIOReturnNotReady;
 }
 if(o.active()){
  const bool accepted=(o.phase==ItlAwdl::NativeSearchOwner::Phase::Starting||awdlNativeSearchEnabled())&&
   !o.stopRequested&&!o.expired&&!o.failure&&awdlIbssWorkPending&&o.matches(awdlIbssWorkerEpoch)&&
   itlAwdlRocNowNs()<o.deadlineNs&&ItlAwdl::gDataPort.owner&&!ItlAwdl::gDataPort.releasePending&&
   !memcmp(o.local,ItlAwdl::gDataPort.registeredLocal,6);
  return accepted?kIOReturnSuccess:kIOReturnNotReady;
 }
 return awdlNativeSearchRequest(false);
}
void ItlIwm::awdlNativeSearchPublish(){
 if(!getMainWorkLoop()||!getMainWorkLoop()->inGate()||!getController())return;
 auto d=OSDictionary::withCapacity(20);if(!d)return;
 using D=ItlIbss::ExperimentObservation;const auto &o=awdlNativeSearch;
 const char *keys[]={"Version","Epoch","WorkerEpoch","Phase","Generation","StartNs","DeadlineNs","StopRequested","Expired","Configured","Failure","WorkerPending","RecoveryState","RecoveryStableMs","PrimaryError","RecoveryError","RecoveryFaults","RecoveryGapCount","ControlRowsLost","BackendEnabled"};
 const uint64_t values[]={1,o.epoch,awdlIbssWorkerEpoch,uint32_t(o.phase),o.generation,o.startNs,o.deadlineNs,
  o.stopRequested,o.expired,o.configuredOnce,uint32_t(o.failure),awdlIbssWorkPending,
  awdlProbeCounters?awdlProbeCounters->ibss.get(D::RecoveryState):0,awdlProbeCounters?awdlProbeCounters->ibss.get(D::RecoveryStableMs):0,
  awdlProbeCounters?awdlProbeCounters->ibss.get(D::PrimaryError):0,awdlProbeCounters?awdlProbeCounters->ibss.get(D::RecoveryError):0,
  awdlProbeCounters?awdlProbeCounters->ibss.get(D::RecoveryFaults):0,awdlProbeCounters?awdlProbeCounters->ibss.get(D::RecoveryGapCount):0,
  awdlProbeCounters?awdlProbeCounters->ibss.control.lost:0,awdlNativeSearchEnabled()};
 bool ok=true;for(unsigned i=0;i<20&&ok;++i){auto n=OSNumber::withNumber(values[i],64);if(!n){ok=false;break;}ok=d->setObject(keys[i],n);n->release();}
 if(ok)getController()->setProperty("AWDLNativeSearch",d);d->release();
}
IOReturn ItlIwm::awdlNativeSearchRequest(bool stop,uint64_t expectedEpoch,bool receiveValidation){
 if(!getMainWorkLoop()||!getMainWorkLoop()->inGate()||dmaDetaching||!awdlProbeCounters)return kIOReturnNotReady;
 auto &o=awdlNativeSearch;auto &s=awdlIbssExclusive;using D=ItlIbss::ExperimentObservation;
 if(stop){
  if(expectedEpoch&&expectedEpoch!=o.epoch)return kIOReturnNotReady;
  if(o.active()){o.stop(o.epoch);getMainCommandGate()->commandWakeup(&awdlIbssWorkPending,false);}
  awdlNativeSearchPublish();return kIOReturnSuccess;
 }
 if(expectedEpoch)return kIOReturnBadArgument;
 auto &late=awdlProbeCounters->ibss.lateAdmission;
 const bool lateCandidate=o.epoch&&o.configuredOnce&&awdlNativeDiscovery.used&&o.phase==ItlAwdl::NativeSearchOwner::Phase::Failed&&!o.failure&&
  ItlIbss::lateRecoveryRetired(s)&&
  (awdlProbeCounters->ibss.recovery.phase==ItlIbss::StaRecoveryPhase::NotStable||
   awdlProbeCounters->ibss.recovery.phase==ItlIbss::StaRecoveryPhase::StableAtDeadline);
 // Keep this proof isolated from earlier diagnostics for the entire HAL lifetime.
 // Boot configuration is not ownership: only an untouched, unarmed template may be displaced.
 const bool dormantTemplate=awdlTemplateTxEnabled&&ItlAwdl::gEngine.modeEnabled&&
  !ItlAwdl::gTemplateRadio.validationArmed&&!awdlRocAttempted&&!ItlAwdl::gEngine.running&&
  !ItlAwdl::gEngine.deadlineNs&&!ItlAwdl::gEngine.cyclesSubmitted&&!ItlAwdl::gEngine.cyclesCompleted&&
  !ItlAwdl::gEngine.cycleFailures&&!ItlAwdl::gEngine.startsTotal&&!ItlAwdl::gEngine.endsTotal&&!ItlAwdl::gEngine.failuresTotal;
 const auto reject=[&](uint32_t reason)->IOReturn{
  if(reason!=15)late=ItlIbss::LateRecoveryAdmission();
  auto d=OSDictionary::withCapacity(8);if(d){
   const char *keys[]={"Version","Epoch","Reason","WorkerEpoch","Phase","FirmwareFlags","ExclusivePhase","HostNs"};
   const uint64_t values[]={1,o.epoch,reason,awdlIbssWorkerEpoch,uint32_t(o.phase),uint32_t(com.sc_flags),uint32_t(s.phase),itlAwdlRocNowNs()};
   bool ok=true;for(unsigned i=0;i<8&&ok;++i){auto n=OSNumber::withNumber(values[i],64);if(!n){ok=false;break;}ok=d->setObject(keys[i],n);n->release();}
   if(ok)getController()->setProperty("AWDLNativeSearchAdmission",d);d->release();
  }
  return kIOReturnNotReady;
 };
 if((!o.reusable()&&!lateCandidate)||awdlIbssWorkPending)return reject(1);
 if(itlIbssDataTrial.everArmed||awdlBatch.enabled||awdlBatch.active())return reject(2);
 if(awdlIbssTxPort||awdlIbssPeerActive())return reject(3);
 if(!awdlProbeEnabled||awdlDiscoveryEnabled)return reject(4);
 if(((awdlTemplateTxEnabled||ItlAwdl::gEngine.modeEnabled)&&!dormantTemplate)||ItlAwdl::gEngine.running)return reject(5);
 if(ItlAwdl::gSingleDataProbe.pending)return reject(6);
 if(!awdlIbssNativeStartIdle())return reject(7);
 auto &previous=awdlProbeCounters->ibss;
 if(!o.epoch){
  if(awdlIbssOneShot.armed||!ItlIbss::untouchedDataTrialReusable(s)||previous.recovery.phase!=ItlIbss::StaRecoveryPhase::Idle)return reject(8);
 }else if(!lateCandidate&&(!ItlIbss::recoveredDataTrialReusable(s)||previous.recovery.phase!=ItlIbss::StaRecoveryPhase::StableAtDeadline))return reject(9);
 if(previous.control.lost||previous.get(D::RecoveryFaults))return reject(10);
 ItlIbssExclusiveTransport transport{this};
 const bool repeat=awdlNativeDiscovery.used||awdlNativeControlTx.phase!=ItlAwdl::NativeSearchControlTx::Idle;
 if(repeat){
  const auto &tx=awdlNativeControlTx;
  if(!awdlNativeDiscovery.canRearm(o.epoch)||tx.epoch!=o.epoch||!tx.reusableTerminal()||
     tx.port||tx.schedule.pending||tx.submitted!=tx.completed||o.failure)return reject(14);
 }
 if(!transport.canBegin())return reject(11);
 if(!transport.administrativelyUp())return reject(12);
 if(!transport.associatedAndAuthorized())return reject(13);
 if(lateCandidate&&!late.sample(itlAwdlRocNowNs(),uint32_t(com.sc_generation),true))return reject(15);
 auto counters=new ItlAwdlProbeCounters;
 if(!counters||!counters->init()){if(counters)counters->release();return kIOReturnNoMemory;}
 counters->configureManagementFilter(awdlMgmtFilterEnabled);
 for(unsigned i=D::NvmChannelFlagsVersion;i<=D::MccChubSource;++i)counters->ibss.set(static_cast<D::Metric>(i),previous.get(static_cast<D::Metric>(i)));
 counters->ibss.set(D::FirmwareFaultTraceVersion,1);counters->ibss.set(D::RecoveryObserverVersion,2);
 const auto timerResult=awdlIbssExperimentTimerPrepare();
 if(timerResult!=kIOReturnSuccess){counters->release();return timerResult;}
 auto reserved=awdlIbssWorkerPrepare();if(!reserved){counters->release();return kIOReturnNoResources;}
 auto job=static_cast<ItlIbssWorkerRequest*>(reserved);auto next=o;
 if(lateCandidate)next.phase=ItlAwdl::NativeSearchOwner::Phase::Cancelled;
 if(!next.commit(job->epoch,ItlAwdl::gDataPort.registeredLocal,itlAwdlRocNowNs(),ItlAwdl::nativeReceiveLimit(receiveValidation),receiveValidation)){
  awdlIbssWorkerDiscard(reserved);counters->release();return kIOReturnBadArgument;
 }
 // The gate remains held; no fallible operation follows task submission.
 if(!awdlIbssWorkerSubmit(reserved)){awdlIbssWorkerDiscard(reserved);counters->release();return kIOReturnNotReady;}
 if(dormantTemplate){awdlRocChannel=0;awdlTemplateTxEnabled=false;awdlAuxTxEnabled=false;ItlAwdl::gEngine.modeEnabled=0;}
 if(repeat){
  const bool sameMaster=awdlNativeControlTx.nativeRoleAccepted && awdlNativeControlTx.periodic &&
      awdlNativeDiscovery.owner==reinterpret_cast<uintptr_t>(ItlAwdl::gDataPort.owner) &&
      !memcmp(awdlNativeDiscovery.local,next.local,6);
  awdlNativeDiscovery.resetAfterRetirement();
  awdlNativeControlTx=ItlAwdl::NativeSearchControlTx();
  awdlNativeControlTx.resumePriorMaster=sameMaster;
 }else{
  awdlNativeControlTx.resumePriorMaster=false; // An unused grant belongs to its first successor only.
 }
 o=next;if(lateCandidate){s.primaryError=0;s.lastError=0;}s.phase=ItlIbss::ExclusivePhase::Idle;s.resetProven=false;s.generation=0;s.deadline=s.recoveryDeadline=0;
 auto old=awdlProbeCounters;__atomic_store_n(&awdlProbeCounters,counters,__ATOMIC_SEQ_CST);
 // Registry publication is diagnostic, not the acceptance or ready contract.
 getController()->setProperty("AWDLBeforeNativeSearch",old);getController()->setProperty("AWDLRxProbe",counters);old->release();
 awdlIbssOneShot=ItlIbss::OneShot();awdlIbssOneShot.armed=awdlIbssOneShot.profile=true;
 counters->ibss.set(D::Armed,1);counters->ibss.set(D::ProfileRequested,1);
 awdlIbssExperimentTimerStart();awdlNativeSearchPublish();return kIOReturnSuccess;
}
void ItlIwm::awdlNativeSearchTick(uint64_t epoch){
 if(!getMainWorkLoop()||!getMainWorkLoop()->inGate()||getMainWorkLoop()->onThread()||dmaDetaching||!awdlProbeCounters||
    epoch!=awdlIbssWorkerEpoch||!awdlNativeSearch.matches(epoch)||!awdlNativeSearch.active())return;
 using P=ItlAwdl::NativeSearchOwner::Phase;using E=ItlIbss::ExclusivePhase;using D=ItlIbss::ExperimentObservation;
 auto &o=awdlNativeSearch;auto &s=awdlIbssExclusive;auto &d=awdlProbeCounters->ibss;
 o.expire(epoch,itlAwdlRocNowNs());
 if(!ItlAwdl::gDataPort.owner||ItlAwdl::gDataPort.releasePending||memcmp(o.local,ItlAwdl::gDataPort.registeredLocal,6))o.stop(epoch);
 if(o.phase==P::Starting){
  ItlIbssExclusiveTransport transport{this};
  if(!transport.administrativelyUp())o.fail(epoch,ECANCELED);
  else if(transport.canBegin()&&transport.associatedAndAuthorized()){
   awdlIbssOneShot.attempted=true;d.armLifecycle(transport.now());d.set(D::Attempted,1);
   const auto now=transport.now();int error=now<o.deadlineNs?awdlIbssExclusiveBegin(o.deadlineNs-now):ETIMEDOUT;
   if(dmaDetaching||!awdlIbssTimer||epoch!=awdlIbssWorkerEpoch)return;
   d.set(D::BeginError,uint32_t(error));
   if(!ItlAwdl::gDataPort.owner||ItlAwdl::gDataPort.releasePending||memcmp(o.local,ItlAwdl::gDataPort.registeredLocal,6))o.stop(epoch);
   if(!ItlIbss::untouchedDataTrialReusable(s))o.recordEntered(epoch,s.generation?s.generation:uint32_t(com.sc_generation));
   if(error)o.fail(epoch,error);
   else if(s.phase==E::FirmwareReady){
    o.expire(epoch,itlAwdlRocNowNs());
    if(o.phase==P::Starting){
     d.arm(s.generation,s.session.resources.mac,itlAwdlRocNowNs(),s.deadline);d.set(D::TargetChannel,0);
     error=awdlIbssCandidateConfigure();if(dmaDetaching||!awdlIbssTimer||epoch!=awdlIbssWorkerEpoch)return;
     if(!ItlAwdl::gDataPort.owner||ItlAwdl::gDataPort.releasePending||memcmp(o.local,ItlAwdl::gDataPort.registeredLocal,6))o.stop(epoch);
     d.set(D::ConfigureError,uint32_t(error));d.set(D::ConfigurationCompleted,!error&&s.session.quotaConfigured);
     o.expire(epoch,itlAwdlRocNowNs());
     if(error)o.fail(epoch,error);else if(!s.session.quotaConfigured)o.fail(epoch,EIO);
     else o.configured(epoch,s.generation,itlAwdlRocNowNs());
    }
   }else o.fail(epoch,EIO);
  }
 }
 awdlNativeControlTxTick();
 awdlNativeDiscoveryTick();
 if(o.phase==P::Stopping){
  if(!o.generation&&ItlIbss::untouchedDataTrialReusable(s)){
   o.finishUnstarted(epoch,true);awdlIbssOneShot.armed=false;awdlNativeSearchPublish();return;
  }
  if(s.ownsRadio&&!s.operation&&!s.resetRequested&&s.phase==E::FirmwareReady){const int error=awdlIbssExclusiveEnd();if(error)o.fail(epoch,error);}
  if(!s.ownsRadio&&!s.operation&&o.generation)o.restoring(epoch,o.generation);
 }
 awdlIbssExperimentTick();if(dmaDetaching||!awdlIbssTimer||epoch!=awdlIbssWorkerEpoch)return;
 o.expire(epoch,itlAwdlRocNowNs());
 if(o.phase==P::Searching&&s.phase!=E::FirmwareReady)o.fail(epoch,s.lastError?s.lastError:EIO);
 if(o.phase==P::Stopping&&!s.ownsRadio&&!s.operation&&o.generation)o.restoring(epoch,o.generation);
 if(o.phase==P::Recovering){
  const bool retired=s.resetProven&&!s.ownsRadio&&!s.operation&&!s.resetRequested&&!s.ordinary.active()&&s.commandAuthority.idle()&&
   !s.session.resources.valid()&&!s.session.commandInFlight&&!s.session.needsReset;
  const auto recovery=d.recovery.phase;
  const bool ended=recovery==ItlIbss::StaRecoveryPhase::StableAtDeadline||recovery==ItlIbss::StaRecoveryPhase::NotStable||
   recovery==ItlIbss::StaRecoveryPhase::Incomplete||recovery==ItlIbss::StaRecoveryPhase::Faulted||
   (recovery==ItlIbss::StaRecoveryPhase::Idle&&(s.phase==E::Failed||s.phase==E::Cancelled));
  if(ended){const bool stable=recovery==ItlIbss::StaRecoveryPhase::StableAtDeadline&&s.phase==E::Associated;
   const bool clean=!s.primaryError&&!s.recoveryError&&!d.control.lost&&!d.get(D::RecoveryFaults)&&!d.get(D::RecoveryGapCount);
   o.finish(epoch,retired,stable,clean);
  }
 }
 awdlNativeSearchPublish();
}
