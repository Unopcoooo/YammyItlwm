// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "ActionFrame.hpp"
#include "IbssWindowSelection.hpp"
#include <kern/thread_call.h>
void ItlIwm::awdlIbssExperimentEvent(unsigned kind,unsigned code,int result,unsigned flags,unsigned status) {
    if (!awdlProbeCounters || !getMainWorkLoop()->inGate() || !awdlIbssOneShot.attempted) return;
    using H = ItlIbss::ExperimentRows;
    H::Row row; const auto us = itlAwdlRocNowNs()/1000;
    row.v[H::Kind]=kind; row.v[H::Code]=code; row.v[H::Result]=uint32_t(result);
    row.v[H::HostUsLow]=uint32_t(us);row.v[H::HostUsHigh]=uint32_t(us>>32);
    row.v[H::Generation]=uint32_t(com.sc_generation);row.v[H::Phase]=unsigned(awdlIbssExclusive.phase);
    row.v[H::Flags]=flags;row.v[H::Status]=status;
    awdlProbeCounters->ibss.control.append(row);
}
void ItlIwm::awdlIbssStaRecoveryObserve() {
    if(!awdlProbeCounters || !getMainWorkLoop()->inGate())return;
    using D=ItlIbss::ExperimentObservation;using H=ItlIbss::ExperimentRows;
    auto &d=awdlProbeCounters->ibss;auto &watch=d.recovery;const auto &exclusive=awdlIbssExclusive;
    if(!awdlIbssOneShot.attempted)return;
    const auto now=itlAwdlRocNowNs();
    const auto beforePhase=watch.phase;const auto beforeGeneration=watch.generation;
    const auto beforeStable=watch.stableMs!=0;
    if(watch.phase==ItlIbss::StaRecoveryPhase::Idle && exclusive.phase==ItlIbss::ExclusivePhase::AwaitAssociation) {
        if(watch.begin(now,uint32_t(com.sc_generation),exclusive.ownsRadio,exclusive.operation,exclusive.nativeSearchRecovery)) {
            __atomic_store_n(&d.recoveryStartNs,watch.start,__ATOMIC_SEQ_CST);
            __atomic_store_n(&d.recoveryDeadlineNs,watch.deadline,__ATOMIC_SEQ_CST);
        }
    }
    if(watch.watching()) {
        ItlIbssExclusiveTransport transport{this};
        const bool eligible=transport.administrativelyUp() && transport.associatedAndAuthorized() && !com.sc_ic.ic_wcl_mac_reconfig;
        // Recovery evidence is in control, not the bounded RF packet prefix.
        const bool lost=__atomic_load_n(&d.control.lost,__ATOMIC_SEQ_CST)!=0;
        watch.sample(now,uint32_t(com.sc_generation),eligible,exclusive.ownsRadio,exclusive.operation,d.get(D::RecoveryFaults),lost);
        // Same-gate snapshots of each original eligibility predicate. Preserve
        // the original eligibility expression/sample call and60s criterion.
        const auto ic=&com.sc_ic;
        const uint32_t mask=(transport.administrativelyUp()?1u:0u)|(dmaDetaching?2u:0u)|
            (ic->ic_state==IEEE80211_S_RUN?4u:0u)|(com.ns_nstate==IEEE80211_S_RUN?8u:0u)|
            ((ic->ic_if.if_flags&IFF_RUNNING)?16u:0u)|(ic->ic_bss?32u:0u)|
            ((ic->ic_flags&IEEE80211_F_RSNON)?64u:0u)|((ic->ic_bss && ic->ic_bss->ni_port_valid)?128u:0u)|
            (ic->ic_wcl_mac_reconfig?256u:0u)|(exclusive.ownsRadio?512u:0u)|
            (exclusive.operation?1024u:0u)|(eligible?2048u:0u);
        const uint32_t states=uint32_t(ic->ic_state)|(uint32_t(com.ns_nstate)<<8);
        const unsigned code=watch.eligibilityTrace.claim(uint32_t(com.sc_generation),mask,states,
            uint32_t(ic->ic_if.if_flags),uint32_t(com.sc_flags),!watch.watching());
        if(code) {
            H::Row row;const auto us=now/1000;row.v[H::Kind]=31;row.v[H::Code]=code;row.v[H::Length]=1;
            row.v[H::HostUsLow]=uint32_t(us);row.v[H::HostUsHigh]=uint32_t(us>>32);
            row.v[H::Generation]=uint32_t(com.sc_generation);row.v[H::Phase]=uint32_t(exclusive.phase);
            row.v[H::Flags]=mask;row.v[H::Status]=states;row.v[H::Result]=uint32_t(watch.phase);
            row.v[H::Detail0]=uint32_t(ic->ic_if.if_flags);row.v[H::Detail1]=uint32_t(com.sc_flags);
            d.control.append(row);
        }
    }
    d.set(D::RecoveryState,uint32_t(watch.phase));d.set(D::RecoveryInitialGeneration,watch.initialGeneration);
    d.set(D::RecoveryGeneration,watch.generation);d.set(D::RecoveryGenerationChanges,watch.generationChanges);
    d.set(D::RecoverySampleCount,watch.samples);d.set(D::RecoveryStableMs,watch.stableMs);d.set(D::RecoveryGapCount,watch.gaps);
    if(watch.phase!=beforePhase || watch.generation!=beforeGeneration || (watch.stableMs!=0)!=beforeStable) {
        H::Row row;const auto us=now/1000;row.v[H::Kind]=15;row.v[H::Code]=uint32_t(watch.phase);
        row.v[H::HostUsLow]=uint32_t(us);row.v[H::HostUsHigh]=uint32_t(us>>32);
        row.v[H::Generation]=watch.generation;row.v[H::Phase]=uint32_t(exclusive.phase);
        row.v[H::Flags]=watch.stableMs!=0;row.v[H::Status]=d.get(D::RecoveryFaults);
        row.v[H::Detail0]=watch.stableMs;row.v[H::Detail1]=watch.generationChanges;
        d.control.append(row);
    }
}
void ItlIwm::awdlIbssExperimentPublish() {
    if(!awdlProbeCounters)return;
    using D=ItlIbss::ExperimentObservation;
    auto &d=awdlProbeCounters->ibss;unsigned phase=unsigned(awdlIbssExclusive.phase);
    d.set(D::PrimaryError,uint32_t(awdlIbssExclusive.primaryError));
    d.set(D::RecoveryError,uint32_t(awdlIbssExclusive.recoveryError));
    awdlIbssStaRecoveryObserve();
    awdlIbssDataPublish();
    if(d.get(D::Phase)!=phase){d.set(D::Phase,phase);awdlIbssExperimentEvent(1,0,awdlIbssExclusive.lastError,0,0);}
}
struct ItlIbssWorkerRequest {ItlIwm *hal;IOCommandGate *gate;IOWorkLoop *loop;thread_call_t call;uint64_t epoch;bool submitted;};
// Reserve all job resources without publishing work or changing the HAL epoch.
void *ItlIwm::awdlIbssWorkerPrepare() {
    if(!getMainWorkLoop()->inGate()||dmaDetaching||!awdlIbssTimer||awdlIbssWorkPending||awdlIbssWorkerEpoch==UINT64_MAX)return nullptr;
    auto job=static_cast<ItlIbssWorkerRequest*>(IOMalloc(sizeof(ItlIbssWorkerRequest)));
    if(!job)return nullptr;
    job->hal=this;job->gate=getMainCommandGate();job->loop=getMainWorkLoop();
    job->epoch=awdlIbssWorkerEpoch+1;job->submitted=false;
    job->call=thread_call_allocate(awdlIbssExperimentWorker,job);
    if(!job->call){IOFree(job,sizeof(*job));return nullptr;}
    retain();job->gate->retain();job->loop->retain();return job;
}
bool ItlIwm::awdlIbssWorkerSubmit(void *reserved) {
    auto job=static_cast<ItlIbssWorkerRequest*>(reserved);
    if(!getMainWorkLoop()->inGate()||dmaDetaching||!awdlIbssTimer||awdlIbssWorkPending||
       !job||job->hal!=this||job->submitted||awdlIbssWorkerEpoch==UINT64_MAX||job->epoch!=awdlIbssWorkerEpoch+1)return false;
    awdlIbssWorkerEpoch=job->epoch;awdlIbssWorkPending=true;job->submitted=true;
    thread_call_enter(job->call);return true;
}
void ItlIwm::awdlIbssWorkerDiscard(void *reserved) {
    auto job=static_cast<ItlIbssWorkerRequest*>(reserved);
    if(!job||job->hal!=this||job->submitted)return;
    thread_call_free(job->call);job->gate->release();job->loop->release();job->hal->release();IOFree(job,sizeof(*job));
}
void ItlIwm::awdlIbssExperimentWorker(void *argument,void *) {
    auto job=static_cast<ItlIbssWorkerRequest*>(argument);
    job->gate->runAction([](OSObject*,void *p,void*,void*,void*) -> IOReturn {
        auto job=static_cast<ItlIbssWorkerRequest*>(p);auto hal=job->hal;
        // Keep the same retained job throughout the lease and recovery. The
        // deadline must never depend on allocating another job under pressure.
        while(hal->awdlIbssWorkPending && hal->awdlIbssWorkerEpoch==job->epoch && !hal->dmaDetaching && hal->awdlIbssTimer) {
            const bool search=hal->awdlNativeSearch.matches(job->epoch);
            if(search)hal->awdlNativeSearchTick(job->epoch);
            else hal->awdlIbssExperimentTick();
            if(hal->awdlIbssWorkerEpoch!=job->epoch)break;
            if(search&&!hal->awdlNativeSearch.active())break;
            if(!search)hal->awdlBatchPoll();
            if(hal->awdlIbssWorkerEpoch!=job->epoch)break;
            using P=ItlIbss::ExclusivePhase;const auto &s=hal->awdlIbssExclusive;
            const bool active=(search&&hal->awdlNativeSearch.active()) || hal->awdlBatch.active() || s.operation || s.phase==P::FirmwareReady || s.phase==P::Restoring ||
                s.phase==P::AwaitAssociation || (s.phase==P::Failed && s.ownsRadio) || hal->awdlProbeCounters->ibss.recovery.watching();
            if((!search && !hal->awdlIbssOneShot.attempted && !hal->awdlBatch.active()) || !active)break;
            uint64_t deadline;uint32_t waitUs=250000;
            const auto &window=hal->awdlProbeCounters->ibss.window;const auto now=itlAwdlRocNowNs();
            if(window.pending())waitUs=window.wakeNs>now ? uint32_t((window.wakeNs-now+999)/1000) : 1;
            if(hal->awdlIbssDataActive()&&itlIbssDataTrial.phase==ItlIbss::DataTrialPhase::Receiving&&
               itlIbssDataTrial.identityState==2&&window.phase==ItlIbss::WindowProbe::Open){
                const auto due=itlIbssDataTrial.identityRefreshNotBeforeNs();
                if(due>now){const auto refreshWait=uint32_t((due-now+999)/1000);if(refreshWait<waitUs)waitUs=refreshWait;}
            }
            if(search&&hal->awdlNativeControlTx.periodic&&hal->awdlNativeControlTx.active()){
                const auto controlWait=hal->awdlNativeControlTx.schedule.wakeUs(now);
                if(controlWait<waitUs)waitUs=controlWait;
            }
            if(search&&hal->awdlNativeControlTx.nativeRoleAccepted&&
               hal->awdlNativeControlTx.active()&&hal->awdlNativeControlTx.epochHostNs){
                const auto awWait=ItlAwdl::NativeAwWindowTracker::nextBoundaryUs(
                    now,hal->awdlNativeControlTx.epochHostNs);
                if(awWait&&awWait<waitUs)waitUs=awWait;
            }
            if(search&&hal->awdlNativeDiscovery.used&&(hal->awdlNativeDiscovery.txCount||hal->awdlNativeDiscovery.port)){
                if(waitUs>1000)waitUs=1000;
            }
            if(waitUs>250000)waitUs=250000;
            if(hal->awdlIbssDataActive()&&(itlIbssDataTrial.controlMode&1)&&itlIbssDataTrial.identityState==4&&
               !itlIbssDataTrial.endRequested&&!itlIbssDataTrial.cancelled){
                const auto controlWait=itlIbssDataTrial.controlBlocked==3?16384:itlIbssDataTrial.controlSchedule.wakeUs(now);
                if(controlWait<waitUs)waitUs=controlWait;
            }
            if(hal->awdlIbssDataActive()&&itlIbssDataTrial.nativeQueueMode&&itlIbssDataTrial.nativeStarted&&
               !itlIbssDataTrial.cancelled&&!itlIbssDataTrial.endRequested&&hal->awdlIbssNativeLeaseReason()==0&&
               window.canPlan(window.lateCloseCancelled(true),true)) {
                const auto planningWait=hal->awdlProbeCounters->ibss.clockWaitUs(now,true);
                if(planningWait<waitUs)waitUs=planningWait;
            }
            clock_interval_to_deadline(waitUs,kMicrosecondScale,&deadline);
            hal->getMainCommandGate()->commandSleep(&hal->awdlIbssWorkPending,deadline,THREAD_UNINT);
        }
        if(hal->awdlIbssWorkerEpoch==job->epoch){hal->awdlIbssWorkPending=false;
            if(hal->awdlNativeSearch.matches(job->epoch))hal->awdlNativeSearchPublish();}
        return kIOReturnSuccess;
    },job);
    job->submitted=false;job->hal->awdlIbssWorkerDiscard(job);
}
void ItlIwm::awdlIbssExperimentTick() {
    if (dmaDetaching || !awdlProbeCounters || !awdlIbssOneShot.armed || awdlIbssExclusive.operation) return;
    if(awdlBatch.enabled && awdlBatch.abortRequested && awdlIbssOneShot.attempted &&
       awdlIbssExclusive.phase==ItlIbss::ExclusivePhase::FirmwareReady)awdlIbssExclusiveEnd();
    ItlIbssExclusiveTransport transport{this};
    if (!getMainWorkLoop()->inGate() || getMainWorkLoop()->onThread()) return;
    awdlIbssTxTick();
    awdlIbssPeerTick();
    awdlIbssDataTick();
    if(awdlBatch.enabled && ItlIbss::batchTxMode(awdlBatch.mode) &&
       awdlIbssExclusive.phase==ItlIbss::ExclusivePhase::FirmwareReady) {
        using T=ItlIbss::ExperimentObservation;const auto &tx=awdlProbeCounters->ibss;
        if(tx.get(T::TxAttempted) && (!tx.get(T::TxAccepted) || tx.get(T::TxCompleted) || tx.get(T::TxDeadlineExpired) || tx.get(T::TxMismatch)))awdlIbssExclusiveEnd();
    }
    awdlIbssExclusivePoll();
    using D=ItlIbss::ExperimentObservation;using H=ItlIbss::ExperimentRows;
    auto &d=awdlProbeCounters->ibss;
    awdlIbssExperimentPublish();
    // Read the same native GP2 register used by AUX diagnostics. This does not
    // submit a firmware command, retune, arm a window or extend the lease.
    auto &clockState=awdlIbssExclusive;auto &window=d.window;
    const bool nativeWindows=awdlIbssDataActive()&&itlIbssDataTrial.nativeQueueMode&&itlIbssDataTrial.nativeStarted&&
        !itlIbssDataTrial.cancelled&&!itlIbssDataTrial.endRequested&&awdlIbssNativeLeaseReason()==0;
    const bool replanLateClose=(awdlIbssDataActive() && itlIbssDataTrial.mayReplanLateClose(window)) ||
        (nativeWindows&&window.lateCloseCancelled(true));
    const bool windowHealthy=!window.invalidated && clockState.ownsRadio && !clockState.operation && !clockState.resetRequested &&
        clockState.phase==ItlIbss::ExclusivePhase::FirmwareReady &&
        !(com.sc_flags & (IWM_FLAG_HW_ERR|IWM_FLAG_RFKILL|IWM_FLAG_SHUTDOWN)) &&
        d.capturesProtocol(uint32_t(com.sc_generation),transport.now());
    auto windowEvent=[&](unsigned code,uint32_t gp2,uint64_t host) {
        H::Row row;const auto us=host/1000;row.v[H::Kind]=18;row.v[H::Code]=code;
        row.v[H::Generation]=uint32_t(com.sc_generation);row.v[H::Phase]=uint32_t(clockState.phase);
        row.v[H::HostUsLow]=uint32_t(us);row.v[H::HostUsHigh]=uint32_t(us>>32);row.v[H::Gp2]=gp2;
        row.v[H::Channel]=6;row.v[H::MacContext]=window.plan.peer;row.v[H::Length]=window.plan.epoch;
        row.v[H::Detail0]=window.plan.open;row.v[H::Detail1]=window.plan.close;row.v[H::FrameControl]=window.plan.streak;
        row.v[H::Flags]=window.rawFrames;row.v[H::Status]=window.peerFrames;row.v[H::Valid]=window.cancelReason;
        d.control.append(row);
    };
    auto windowCancelEvidence=[&](unsigned code,uint32_t gp2,uint64_t host,bool locked) {
        H::Row row;const auto us=host/1000;
        row.v[H::Kind]=37;row.v[H::Code]=code;row.v[H::Valid]=1;
        row.v[H::HostUsLow]=uint32_t(us);row.v[H::HostUsHigh]=uint32_t(us>>32);
        row.v[H::Generation]=uint32_t(com.sc_generation);row.v[H::Gp2]=gp2;
        row.v[H::MacContext]=window.plan.peer;row.v[H::Length]=window.plan.epoch;
        row.v[H::Detail0]=window.plan.open;row.v[H::Detail1]=window.plan.close;
        row.v[H::Phase]=uint32_t(clockState.phase);row.v[H::Result]=window.cancelReason;
        row.v[H::Status]=com.sc_flags & (IWM_FLAG_HW_ERR|IWM_FLAG_RFKILL|IWM_FLAG_SHUTDOWN);
        row.v[H::Flags]=(window.invalidated?1u:0u) | (!clockState.ownsRadio?2u:0u) |
            (clockState.operation?4u:0u) | (clockState.resetRequested?8u:0u) |
            (clockState.phase!=ItlIbss::ExclusivePhase::FirmwareReady?16u:0u) |
            (row.v[H::Status]?32u:0u) | (!d.capturesProtocol(uint32_t(com.sc_generation),host)?64u:0u) |
            (!locked?128u:0u) | (uint32_t(com.sc_generation)!=window.plan.generation?256u:0u) |
            (host>=clockState.deadline?512u:0u);
        d.control.append(row);
    };
    if(window.pending() && !windowHealthy){window.cancel(1);windowCancelEvidence(1,0,transport.now(),true);windowEvent(4,0,transport.now());}
    const bool windowDue=window.pending() && transport.now()>=window.wakeNs;
    if(clockState.ownsRadio && !clockState.operation && !clockState.resetRequested &&
       clockState.phase==ItlIbss::ExclusivePhase::FirmwareReady &&
       !(com.sc_flags & (IWM_FLAG_HW_ERR|IWM_FLAG_RFKILL|IWM_FLAG_SHUTDOWN)) &&
       d.claimClockSample(uint32_t(com.sc_generation),transport.now(),windowDue,nativeWindows)) {
        const uint64_t before=itlAwdlRocNowNs();
        const bool locked=iwm_nic_lock(&com);
        uint32_t gp2=0;if(locked){gp2=iwm_read_prph(&com,IWM_DEVICE_SYSTEM_TIME_REG);iwm_nic_unlock(&com);}
        const uint64_t after=itlAwdlRocNowNs(),mid=before+(after-before)/2,us=mid/1000;
        H::Row row;row.v[H::Kind]=17;row.v[H::Generation]=uint32_t(com.sc_generation);
        row.v[H::Phase]=uint32_t(clockState.phase);row.v[H::HostUsLow]=uint32_t(us);row.v[H::HostUsHigh]=uint32_t(us>>32);
        row.v[H::Flags]=uint32_t(locked);row.v[H::Result]=locked?0:EBUSY;
        row.v[H::Gp2]=gp2;row.v[H::Status]=uint32_t((after-before+999)/1000);row.v[H::Code]=IWM_DEVICE_SYSTEM_TIME_REG;
        row.v[H::Channel]=d.get(D::TargetChannel);row.v[H::MacContext]=clockState.session.resources.mac;
        row.v[H::Detail0]=uint32_t(before/1000);row.v[H::Detail1]=uint32_t(after/1000);
        if((!awdlIbssDataActive()||!itlIbssDataTrial.passive()) && (!awdlBatch.enabled || awdlBatch.mode==ItlIbss::BatchMode::Window) && (!awdlIbssDataActive() || (itlIbssDataTrial.prepared && (itlIbssDataTrial.phase==ItlIbss::DataTrialPhase::WaitingWindow || (itlIbssDataTrial.nativeQueueMode&&!itlIbssDataTrial.endRequested))) || (itlIbssDataTrial.phase==ItlIbss::DataTrialPhase::Receiving && itlIbssDataTrial.identityState==0)) && window.canPlan(replanLateClose,nativeWindows) && locked && d.get(D::TargetChannel)==6) {
            ItlIbss::WindowPlan plan;uint32_t reason=0;
            const auto previousClose=window.plan.close;
            if(awdlProbeCounters->planIbssWindow(uint32_t(com.sc_generation),gp2,after,clockState.deadline,
                                                awdlNativeSearch.active()?awdlNativeSearch.local:com.sc_ic.ic_myaddr,plan,reason,awdlIbssDataActive()?itlIbssDataTrial.request.peer:nullptr) &&
               window.arm(plan,uint32_t(com.sc_generation),gp2,after,clockState.deadline,replanLateClose,nativeWindows)) {
                if(replanLateClose)awdlIbssExperimentEvent(50,1,0,window.attempts,previousClose);
                windowEvent(1,gp2,after);
                for(unsigned part=0;part<4;++part)d.control.append(ItlIbss::selectionRow(window.plan,after,part));
            }
            row.v[H::Valid]=reason;
        } else if(windowDue) {
            const auto code=window.sample(uint32_t(com.sc_generation),gp2,after,clockState.deadline,locked && windowHealthy);
            if(code==4)windowCancelEvidence(2,gp2,after,locked);
            if(code)windowEvent(code,gp2,after);
        }
        // Faster polling must not exhaust the fixed evidence ring. Window events remain complete.
        if(!nativeWindows || windowDue || (d.clockSampleCount%16u)==0)d.control.append(row);
    } else if(windowDue) {window.cancel(3);windowCancelEvidence(3,0,transport.now(),true);windowEvent(4,0,transport.now());}
    awdlIbssDataWindow();
    if(awdlBatch.enabled && awdlBatch.abortRequested)return;
    if (!awdlIbssOneShot.due(transport.now(),transport.associatedAndAuthorized(),transport.generation(),
        transport.canBegin() && !awdlIbssExclusive.ordinary.active())) return;
    d.armLifecycle(transport.now());
    d.set(D::Attempted,1); awdlIbssExperimentEvent(H::State,1,0,0,0);
    int error=awdlIbssExclusiveBegin(20000000000ull);
    if (!awdlProbeCounters || dmaDetaching) return;
    awdlProbeCounters->ibss.set(D::BeginError,uint32_t(error));
    awdlIbssExperimentPublish();
    awdlIbssExperimentEvent(H::State,2,error,0,0);
    if (!error && awdlIbssExclusive.phase==ItlIbss::ExclusivePhase::FirmwareReady) {
        if(!awdlIbssOneShot.profile) {
            // First repaired trial proves bootstrap and ordinary recovery only.
            int endError=awdlIbssExclusiveEnd();awdlIbssExperimentPublish();
            awdlIbssExperimentEvent(H::State,4,endError,0,0);return;
        }
        auto &s=awdlIbssExclusive;
        awdlProbeCounters->ibss.arm(s.generation,s.session.resources.mac,transport.now(),s.deadline);
        d.set(D::TargetChannel,0);
        error=awdlIbssCandidateConfigure();
        if (!awdlProbeCounters || dmaDetaching) return;
        awdlProbeCounters->ibss.set(D::ConfigureError,uint32_t(error));
        awdlProbeCounters->ibss.set(D::ConfigurationCompleted,!error && s.session.quotaConfigured);
        awdlIbssExperimentEvent(H::State,3,error,0,0);
        if(!error && awdlBatch.enabled && ItlIbss::batchTxMode(awdlBatch.mode)) {
            const int txError=awdlIbssTxProbeOnce();
            awdlIbssExperimentEvent(35,1,txError,awdlBatch.run,0);
            if(txError)awdlIbssExclusiveEnd();
        }
        if (error) awdlIbssExclusiveEnd();
        awdlIbssDataTick();
        awdlIbssExperimentPublish();
    }
}
// Reserve an owned, unscheduled event source before publishing a new request.
IOReturn ItlIwm::awdlIbssExperimentTimerPrepare() {
    if(!getMainWorkLoop()->inGate()||dmaDetaching)return kIOReturnNotReady;
    if(awdlIbssTimer)return kIOReturnSuccess;
    auto timer=IOTimerEventSource::timerEventSource(this,awdlIbssExperimentTimerAction);
    if(!timer)return kIOReturnNoResources;
    const auto result=getMainWorkLoop()->addEventSource(timer);
    if(result!=kIOReturnSuccess){timer->release();return result;}
    awdlIbssTimer=timer;return kIOReturnSuccess;
}
void ItlIwm::awdlIbssExperimentTimerStart() {
    ItlAwdlRocLifetime lifetime(this,getMainCommandGate(),getMainWorkLoop());
    if (!getMainWorkLoop()->inGate()) {
        getMainCommandGate()->runAction([](OSObject*,void *p,void*,void*,void*) -> IOReturn {
            static_cast<ItlIwm*>(p)->awdlIbssExperimentTimerStart();return kIOReturnSuccess;
        },this);return;
    }
    if ((!awdlIbssOneShot.armed && !awdlBatch.active()) || dmaDetaching) return;
    if(awdlIbssExperimentTimerPrepare()!=kIOReturnSuccess) {
        awdlIbssOneShot.armed=false;
        if(awdlProbeCounters) {awdlProbeCounters->ibss.set(ItlIbss::ExperimentObservation::Armed,0);
            awdlProbeCounters->ibss.set(ItlIbss::ExperimentObservation::BeginError,ENOMEM);}
        return;
    }
    awdlIbssTimer->setTimeoutMS(250);
}
void ItlIwm::awdlIbssExperimentTimerStop() {
    if (!awdlIbssTimer) return;
    ItlAwdlRocLifetime lifetime(this,getMainCommandGate(),getMainWorkLoop());
    if (!getMainWorkLoop()->inGate()) {
        getMainCommandGate()->runAction([](OSObject*,void *p,void*,void*,void*) -> IOReturn {
            static_cast<ItlIwm*>(p)->awdlIbssExperimentTimerStop();return kIOReturnSuccess;
        },this);return;
    }
    auto timer=awdlIbssTimer;awdlIbssTimer=nullptr;
    timer->cancelTimeout();getMainWorkLoop()->removeEventSource(timer);timer->release();
}
void ItlIwm::awdlIbssExperimentTimerAction(OSObject *owner,IOTimerEventSource *sender) {
    auto hal=static_cast<ItlIwm*>(owner);
    ItlAwdlRocLifetime lifetime(hal,hal->getMainCommandGate(),hal->getMainWorkLoop());
    sender->retain();
    if (hal->awdlIbssTimer==sender && !hal->dmaDetaching) {
        // Timer is signal-only. Sleeping on this event thread blocks the
        // interrupt source even if commandSleep releases the gate.
        using P=ItlIbss::ExclusivePhase;const auto &s=hal->awdlIbssExclusive;
        const bool pending=hal->awdlBatch.active() || (!hal->awdlBatch.enabled && hal->awdlIbssOneShot.armed && !hal->awdlIbssOneShot.attempted) || s.operation ||
            s.phase==P::FirmwareReady || s.phase==P::Restoring || s.phase==P::AwaitAssociation ||
            (s.phase==P::Failed && s.ownsRadio) || hal->awdlProbeCounters->ibss.recovery.watching();
        if(pending && !hal->awdlIbssWorkPending && (!hal->awdlBatch.enabled || hal->awdlBatch.active())) {
            auto job=hal->awdlIbssWorkerPrepare();
            if(job&&!hal->awdlIbssWorkerSubmit(job))hal->awdlIbssWorkerDiscard(job);
        }
        // Independent of if_timer/IFF_RUNNING and of the old AUX deadline.
        if(hal->awdlIbssTimer==sender && !hal->dmaDetaching && pending) sender->setTimeoutMS(250);
    }
    sender->release();
}
void ItlIwm::awdlIbssExperimentWire(const void *packet,size_t length,bool mq) {
    if(!awdlProbeCounters || !awdlIbssExclusive.ownsRadio) return;
    auto &d=awdlProbeCounters->ibss; const auto ns=itlAwdlRocNowNs();
    if(!d.captures(uint32_t(com.sc_generation),ns)) return;
    using D=ItlIbss::ExperimentObservation;using H=ItlIbss::ExperimentRows;
    if(!getMainWorkLoop()->inGate()) {d.add(D::OutOfGateRx);return;}
    auto m=ItlAwdl::decodeRawRx(packet,length,mq,le16toh(com.sc_last_phy_info.channel),le32toh(com.sc_last_phy_info.system_timestamp));
    if(awdlIbssDataActive()&&itlIbssDataTrial.generation==uint32_t(com.sc_generation)&&m.channel==6)
        itlIbssDataTrial.observeRawData(m.fcValid,m.frameControl);
    if(m.channel==6&&m.fcValid)awdlNativeDiscovery.observe(true,m.frameControl);
    d.add(D::RawIngress);if(m.channel==d.get(D::TargetChannel))d.add(D::RawTargetChannel);if(m.channel==6)d.add(D::RawChannel6);
    if(m.channel==6)d.window.raw(uint32_t(com.sc_generation),m.gp2);
    H::Row r;auto us=ns/1000;r.v[H::HostUsLow]=uint32_t(us);r.v[H::HostUsHigh]=uint32_t(us>>32);
    r.v[H::Flags]=d.get(D::ConfigurationCompleted);r.v[H::Kind]=H::RawRx;r.v[H::Generation]=uint32_t(com.sc_generation);r.v[H::Phase]=unsigned(awdlIbssExclusive.phase);
    r.v[H::Channel]=m.channel;r.v[H::Gp2]=m.gp2;r.v[H::MacContext]=m.macContext;
    r.v[H::Status]=m.status;r.v[H::Valid]=m.descriptorValid | (m.lengthValid<<1) | (m.statusValid<<2) | (m.fcValid<<3);
    r.v[H::Length]=m.length;r.v[H::FrameControl]=m.frameControl;r.v[H::Detail0]=m.filterMatch;r.v[H::Detail1]=m.macPhyIndex;
    d.rx.append(r);
}
void ItlIwm::awdlIbssExperimentCommon(mbuf_t frame,int channel,uint32_t gp2,uint32_t status) {
    if(!awdlProbeCounters || !frame || !awdlIbssExclusive.ownsRadio) return;
    auto &d=awdlProbeCounters->ibss;auto ns=itlAwdlRocNowNs();
    if(!d.captures(uint32_t(com.sc_generation),ns)) return;
    using D=ItlIbss::ExperimentObservation;using H=ItlIbss::ExperimentRows;
    if(!getMainWorkLoop()->inGate()){d.add(D::OutOfGateRx);return;}
    d.add(D::CommonFrames);if(uint32_t(channel)==d.get(D::TargetChannel))d.add(D::CommonTargetChannel);if(channel==6)d.add(D::CommonChannel6);
    const auto n=mbuf_len(frame);const auto p=static_cast<const uint8_t*>(mbuf_data(frame));
    const bool whole=mbuf_next(frame)==nullptr&&n==mbuf_pkthdr_len(frame)&&n<=4096;
    const bool crcOk=(status&(IWM_RX_MPDU_RES_STATUS_CRC_OK|IWM_RX_MPDU_RES_STATUS_OVERRUN_OK))==(IWM_RX_MPDU_RES_STATUS_CRC_OK|IWM_RX_MPDU_RES_STATUS_OVERRUN_OK);
    if(channel==6&&p&&n>=2)awdlNativeDiscovery.observe(false,ItlAwdl::dataLe16(p),whole);
    if(whole&&crcOk&&awdlIbssDataActive()){
        auto &trial=itlIbssDataTrial;
        if(awdlListenerActive()&&!trial.cancelled&&!trial.endRequested&&trial.generation==uint32_t(com.sc_generation)&&channel==6){
            const auto match=trial.listenerPair.observe(p,n,true);
            if(match){
                H::Row event;const auto us=ns/1000;event.v[H::Kind]=56;event.v[H::Code]=match;event.v[H::Valid]=1;
                event.v[H::HostUsLow]=uint32_t(us);event.v[H::HostUsHigh]=uint32_t(us>>32);
                event.v[H::Generation]=uint32_t(com.sc_generation);event.v[H::Channel]=uint32_t(channel);event.v[H::Gp2]=gp2;
                event.v[H::Phase]=unsigned(awdlIbssExclusive.phase);event.v[H::FrameControl]=ItlAwdl::dataLe16(p);
                event.v[H::Flags]=trial.listenerPair.identifier;event.v[H::Status]=trial.listenerPair.lastSequence;
                event.v[H::Detail0]=trial.listenerPair.lastPayloadHash;event.v[H::Length]=uint32_t(n);
                d.control.append(event);
            }
        }
        const auto before=itlIbssDataTrial.targetRx.counts[ItlAwdl::TargetStimulusMatched];
        itlIbssDataTrial.observeTargetFrame(p,n,uint32_t(com.sc_generation),unsigned(channel));
        if(itlIbssDataTrial.targetRx.counts[ItlAwdl::TargetStimulusMatched]!=before)
            awdlIbssExperimentEvent(53,1,0,itlIbssDataTrial.targetRx.lastIdentifier,itlIbssDataTrial.targetRx.lastSequence);
    }
    bool fcsVerified=false;const auto observedLength=whole?ItlAwdl::observerFrameLength(p,n,awdlListenerActive(),fcsVerified):n;
    if(awdlIbssDataActive()&&itlIbssDataTrial.generation==uint32_t(com.sc_generation)&&channel==6)
        itlIbssDataTrial.observeCommonData(p,n);
    ItlAwdl::Summary summary={};
    const bool valid=uint32_t(channel)==d.get(D::TargetChannel) && mbuf_next(frame)==nullptr && n<=4096 &&
        ItlAwdl::inspect(p,observedLength,summary)==ItlAwdl::Result::Valid;
    H::Row r;auto us=ns/1000;r.v[H::HostUsLow]=uint32_t(us);r.v[H::HostUsHigh]=uint32_t(us>>32);
    r.v[H::Flags]=d.get(D::ConfigurationCompleted);r.v[H::Kind]=H::CommonRx;r.v[H::Generation]=uint32_t(com.sc_generation);r.v[H::Phase]=unsigned(awdlIbssExclusive.phase);
    r.v[H::Channel]=uint32_t(channel);r.v[H::Gp2]=gp2;r.v[H::Status]=status;r.v[H::Valid]=valid;
    r.v[H::Length]=uint32_t(n);if(n>=2)r.v[H::FrameControl]=ItlAwdl::rawRx16(p);
    d.rx.append(r);
    if(awdlIbssDataActive()&&channel==6&&mbuf_next(frame)==nullptr&&n==mbuf_pkthdr_len(frame)&&
       (status&(IWM_RX_MPDU_RES_STATUS_CRC_OK|IWM_RX_MPDU_RES_STATUS_OVERRUN_OK))==
        (IWM_RX_MPDU_RES_STATUS_CRC_OK|IWM_RX_MPDU_RES_STATUS_OVERRUN_OK))
        itlIbssNativeReceive(this,p,n);
    if(awdlIbssDataActive()&&channel==6&&mbuf_next(frame)==nullptr&&n==mbuf_pkthdr_len(frame)&&
       (status&(IWM_RX_MPDU_RES_STATUS_CRC_OK|IWM_RX_MPDU_RES_STATUS_OVERRUN_OK))==
        (IWM_RX_MPDU_RES_STATUS_CRC_OK|IWM_RX_MPDU_RES_STATUS_OVERRUN_OK)&&
       itlIbssDataTrial.observeReply(p,itlIbssDataTrial.listenerTx?observedLength:n,uint32_t(com.sc_generation),unsigned(channel),gp2,ns)){
        awdlIbssExperimentEvent(46,2,0,uint32_t(itlIbssDataTrial.listenerTx?observedLength:n),gp2);
        getMainCommandGate()->commandWakeup(&awdlIbssWorkPending,false);
    }
    if(valid&&whole&&crcOk&&awdlIbssDataActive())itlIbssFixedObserve(this,p,observedLength,uint32_t(com.sc_generation),gp2,ns);
    if(valid && channel==6)d.add(D::ValidEnvelopes6);
    if(valid && d.add(D::ValidEnvelopesTarget)<=32){r.v[H::Kind]=H::AwdlEnvelope;d.control.append(r);}
}
void ItlIwm::awdlIbssExperimentBeacon(const void *packet,size_t length) {
    if(!awdlProbeCounters || !awdlIbssExclusive.ownsRadio) return;
    auto &d=awdlProbeCounters->ibss;auto ns=itlAwdlRocNowNs();
    if(!d.captures(uint32_t(com.sc_generation),ns)) return;
    using D=ItlIbss::ExperimentObservation;using H=ItlIbss::ExperimentRows;
    if(!getMainWorkLoop()->inGate()){d.add(D::OutOfGateRx);return;}
    auto b=ItlIbss::decodeShortBeacon(static_cast<const uint8_t*>(packet),length,isset(com.sc_ucode_api,46));
    d.add(D::BeaconNotifications);if(b.valid)d.add(D::BeaconDecoded);
    if(b.valid && (b.status&0xff)==1)d.add(D::BeaconReportedSuccess);
    H::Row r;auto us=ns/1000;r.v[H::HostUsLow]=uint32_t(us);r.v[H::HostUsHigh]=uint32_t(us>>32);
    r.v[H::Flags]=d.get(D::ConfigurationCompleted);r.v[H::Kind]=H::BeaconNotification;r.v[H::Generation]=uint32_t(com.sc_generation);r.v[H::Phase]=unsigned(awdlIbssExclusive.phase);
    r.v[H::Length]=uint32_t(length);r.v[H::Valid]=b.valid;r.v[H::Status]=b.status;r.v[H::Gp2]=b.gp2;
    r.v[H::MacContext]=b.manager;r.v[H::Detail0]=uint32_t(b.tsf);r.v[H::Detail1]=uint32_t(b.tsf>>32);d.control.append(r);
}
