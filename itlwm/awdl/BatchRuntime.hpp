// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "TxTrial.hpp"
void ItlIwm::awdlBatchPublish() {
    if(!awdlProbeCounters)return;
    awdlProbeCounters->setBatch(0,awdlBatch.enabled?ItlIbss::BatchVersion:0);
    awdlProbeCounters->setBatch(1,awdlBatch.run);
    awdlProbeCounters->setBatch(2,uint32_t(awdlBatch.mode));
    awdlProbeCounters->setBatch(3,uint32_t(awdlBatch.phase));
    awdlProbeCounters->setBatch(4,awdlBatch.abortRequested);
    awdlProbeCounters->setBatch(5,awdlBatch.archived);
    awdlProbeCounters->setBatch(6,__atomic_load_n(&awdlBatch.faultSerial,__ATOMIC_SEQ_CST));
    awdlProbeCounters->setBatch(7,ItlIbss::BatchMaxRuns);
}
IOReturn ItlIwm::awdlBatchRequest(uint32_t command,uint32_t run,uint32_t mode) {
    if(awdlNativeSearchReserved() || !awdlBatch.enabled || !awdlProbeCounters || dmaDetaching)return kIOReturnNotReady;
    if(!getMainWorkLoop()->inGate()) {
        struct Call {ItlIwm *hal;uint32_t command,run,mode;} c{this,command,run,mode};
        return getMainCommandGate()->runAction([](OSObject *,void *arg,void *,void *,void *)->IOReturn {
            auto p=static_cast<Call *>(arg);return p->hal->awdlBatchRequest(p->command,p->run,p->mode);
        },&c);
    }
    using namespace ItlIbss;using D=ExperimentObservation;
    if(command==2) { // Acknowledge an externally saved terminal record.
        if(mode || run!=awdlBatch.run || !batchTerminal(awdlBatch.phase) || awdlIbssWorkPending)return kIOReturnNotReady;
        awdlBatch.archived=run;awdlBatchPublish();return kIOReturnSuccess;
    }
    if(command==3) { // Request cancellation; worker owns hardware recovery.
        if(mode || run!=awdlBatch.run || !awdlBatch.active())return kIOReturnNotReady;
        awdlBatch.abortRequested=true;
        getMainCommandGate()->commandWakeup(&awdlIbssWorkPending,false);
        awdlBatchPublish();return kIOReturnSuccess;
    }
    if(command!=1 || run!=awdlBatch.run+1 || !batchModeValid(mode))return kIOReturnBadArgument;
    if(batchTxMode(static_cast<BatchMode>(mode)) && (!awdlBatch.receivePassed || awdlBatch.txUsed))return kIOReturnNotReady;
    if(awdlIbssTxPort || awdlIbssDataActive() || awdlIbssPeerActive())return kIOReturnNotReady;
    if((run==1 && mode!=1) || (mode>=3 && !awdlBatch.bootstrapPassed))return kIOReturnNotReady;
    ItlIbssExclusiveTransport transport{this};auto &s=awdlIbssExclusive;
    const bool healthy=transport.administrativelyUp() && transport.associatedAndAuthorized() &&
        !(com.sc_flags&(IWM_FLAG_HW_ERR|IWM_FLAG_RFKILL|IWM_FLAG_SHUTDOWN)) && !com.sc_ic.ic_wcl_mac_reconfig;
    const bool recovered=!awdlBatch.run || awdlProbeCounters->ibss.recovery.phase==StaRecoveryPhase::StableAtDeadline;
    bool queuesEmpty=true;
    for(unsigned i=0;i<2;++i){unsigned q=i?9:3;queuesEmpty=queuesEmpty && !com.txq[q].queued && com.txq[q].cur==com.txq[q].tail;}
    if(!batchCanStart(awdlBatch.run,awdlBatch.phase,awdlIbssWorkPending,s.ownsRadio,s.operation,
        s.ordinary.active()!=0,healthy,recovered,__atomic_load_n(&awdlBatch.faultSerial,__ATOMIC_SEQ_CST)!=0,
        awdlBatch.archived==awdlBatch.run,queuesEmpty))return kIOReturnNotReady;
    // Objects are never reset in place. IORegistry pins every prior run until
    // controller teardown, including any concurrent serializer/off-gate reader.
    auto next=new ItlAwdlProbeCounters;
    if(!next || !next->init()){if(next)next->release();return kIOReturnNoMemory;}
    next->configureManagementFilter(awdlMgmtFilterEnabled);
    auto previous=awdlProbeCounters;
    for(unsigned i=D::NvmChannelFlagsVersion;i<=D::MccChubSource;++i)
        next->ibss.set(static_cast<D::Metric>(i),previous->ibss.get(static_cast<D::Metric>(i)));
    next->ibss.set(D::FirmwareFaultTraceVersion,1);next->ibss.set(D::RecoveryObserverVersion,2);
    char name[32];snprintf(name,sizeof(name),"AWDLBatchRun%u",awdlBatch.run);
    if(!getController()->setProperty(name,previous) || !getController()->setProperty("AWDLRxProbe",next)) {
        next->release();return kIOReturnNoMemory;
    }
    __atomic_store_n(&awdlProbeCounters,next,__ATOMIC_SEQ_CST);previous->release();
    // Keep the live ordinary-operation counter and command authority intact.
    // No previous exclusive work is pending; a new begin still performs its
    // original stop/DMA proof before reserving any firmware resource.
    s.phase=ExclusivePhase::Idle;s.resetRequested=false;s.resetProven=false;
    s.lastError=s.primaryError=s.recoveryError=0;s.generation=0;s.deadline=s.recoveryDeadline=0;
    s.session=IwmSession();awdlIbssOneShot=OneShot();
    if(batchTxMode(static_cast<BatchMode>(mode)))awdlBatch.txUsed=true;
    awdlBatch.run=run;awdlBatch.mode=static_cast<BatchMode>(mode);awdlBatch.abortRequested=false;
    awdlBatch.requestedNs=itlAwdlRocNowNs();awdlBatch.deadlineNs=awdlBatch.requestedNs+90000000000ull;
    awdlBatch.phase=BatchPhase::Waiting;
    if(awdlBatch.mode==BatchMode::Baseline) {
        awdlBatch.phase=BatchPhase::Running;
        next->ibss.set(D::PassiveTrace,1);next->ibss.armLifecycle(awdlBatch.requestedNs,60000000000ull);
        next->ibss.recovery.begin(awdlBatch.requestedNs,uint32_t(com.sc_generation),false,false);
        next->ibss.recoveryStartNs=next->ibss.recovery.start;next->ibss.recoveryDeadlineNs=next->ibss.recovery.deadline;
    } else {
        awdlIbssOneShot.armed=true;awdlIbssOneShot.profile=awdlBatch.mode!=BatchMode::Bootstrap;
        next->ibss.set(D::Armed,1);next->ibss.set(D::ProfileRequested,awdlIbssOneShot.profile);
    }
    awdlBatchPublish();awdlIbssExperimentTimerStart();
    if(!awdlIbssTimer){awdlBatch.phase=BatchPhase::Failed;awdlBatchPublish();return kIOReturnNoResources;}
    return kIOReturnSuccess;
}
void ItlIwm::awdlBatchPoll() {
    using namespace ItlIbss;using D=ExperimentObservation;
    if(!awdlBatch.enabled || !awdlBatch.active() || !awdlProbeCounters)return;
    auto &d=awdlProbeCounters->ibss;auto &s=awdlIbssExclusive;
    const auto now=itlAwdlRocNowNs();ItlIbssExclusiveTransport transport{this};
    if(awdlBatch.mode==BatchMode::Baseline) {
        d.recovery.sample(now,uint32_t(com.sc_generation),transport.associatedAndAuthorized() &&
            transport.administrativelyUp() && !com.sc_ic.ic_wcl_mac_reconfig,s.ownsRadio,s.operation,
            __atomic_load_n(&awdlBatch.faultSerial,__ATOMIC_SEQ_CST),d.control.lost!=0);
        d.set(D::RecoveryState,uint32_t(d.recovery.phase));d.set(D::RecoveryStableMs,d.recovery.stableMs);
        d.set(D::RecoverySampleCount,d.recovery.samples);
        d.set(D::RecoveryInitialGeneration,d.recovery.initialGeneration);d.set(D::RecoveryGeneration,d.recovery.generation);
        d.set(D::RecoveryGenerationChanges,d.recovery.generationChanges);d.set(D::RecoveryGapCount,d.recovery.gaps);
        if(awdlBatch.abortRequested) {
            d.recovery.phase=StaRecoveryPhase::Incomplete;d.set(D::RecoveryState,uint32_t(d.recovery.phase));
            __atomic_store_n(&d.lifecycleDeadlineNs,now,__ATOMIC_SEQ_CST);
            awdlBatch.phase=BatchPhase::Aborted;
        } else if(!d.recovery.watching())
            awdlBatch.phase=d.recovery.phase==StaRecoveryPhase::StableAtDeadline?BatchPhase::Complete:BatchPhase::Failed;
    } else if(!awdlIbssOneShot.attempted) {
        if(awdlBatch.abortRequested || now>=awdlBatch.deadlineNs) {
            awdlIbssOneShot.armed=false;d.set(D::Armed,0);
            awdlBatch.phase=awdlBatch.abortRequested?BatchPhase::Aborted:BatchPhase::Failed;
        }
    } else {
        awdlBatch.phase=s.ownsRadio?BatchPhase::Running:BatchPhase::Recovering;
        const auto end=__atomic_load_n(&d.lifecycleDeadlineNs,__ATOMIC_SEQ_CST);
        if(end && now>=end && !s.operation && !s.ownsRadio && !d.recovery.watching()) {
            const bool txOK=!batchTxMode(awdlBatch.mode) || txTrialSucceeded(d,awdlBatch.run,awdlBatch.mode);
            const bool ok=txOK && d.recovery.phase==StaRecoveryPhase::StableAtDeadline && !d.control.lost &&
                !d.get(D::BeginError) && !d.get(D::ConfigureError) && !d.get(D::PrimaryError) &&
                !d.get(D::RecoveryError) && !__atomic_load_n(&awdlBatch.faultSerial,__ATOMIC_SEQ_CST);
            awdlBatch.phase=awdlBatch.abortRequested?BatchPhase::Aborted:ok?BatchPhase::Complete:BatchPhase::Failed;
            if(ok && !awdlBatch.abortRequested && awdlBatch.mode==BatchMode::Bootstrap)awdlBatch.bootstrapPassed=true;
            if(ok && !awdlBatch.abortRequested && awdlBatch.mode==BatchMode::Receive && d.get(D::ValidEnvelopesTarget))awdlBatch.receivePassed=true;
            awdlIbssOneShot.armed=false;d.set(D::Armed,0);
        }
    }
    awdlBatchPublish();
}
