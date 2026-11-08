// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// No allocation, command submission, gate acquisition or address export here.
// Ordinary lifecycle entry points may run outside the main gate. Event rows
// use atomic counters/ring writes; raw driver fields are individual snapshots.
void ItlIwm::awdlIbssLifecycleSource(ItlIbss::LifeCode code,uint32_t detail) {
    if(code==ItlIbss::LifeCode::IrqSwError || code==ItlIbss::LifeCode::IrqHwError ||
       code==ItlIbss::LifeCode::IrqRfKill || code==ItlIbss::LifeCode::IrqDmaError || code==ItlIbss::LifeCode::WatchdogReset)
        __atomic_add_fetch(&awdlBatch.faultSerial,1u,__ATOMIC_SEQ_CST);
    auto counters=__atomic_load_n(&awdlProbeCounters,__ATOMIC_SEQ_CST);if(!counters)return;
    auto &d=counters->ibss;const auto now=itlAwdlRocNowNs();
    if(!d.lifecycleCaptures(now))return;
    const bool gated=getMainWorkLoop()->inGate();
    uint32_t context=gated?ItlIbss::InGate:ItlIbss::PublishedPhase;
    if(getMainWorkLoop()->onThread())context|=ItlIbss::OnEventThread;
    if(awdlIbssExclusive.ownsRadio)context|=ItlIbss::OwnsRadio;
    if(com.sc_ic.ic_wcl_mac_reconfig)context|=ItlIbss::MacPending;
    const uint32_t phase=gated?uint32_t(awdlIbssExclusive.phase):d.get(ItlIbss::ExperimentObservation::Phase);
    if(code==ItlIbss::LifeCode::IrqSwError || code==ItlIbss::LifeCode::IrqHwError ||
       code==ItlIbss::LifeCode::IrqRfKill || code==ItlIbss::LifeCode::IrqDmaError || code==ItlIbss::LifeCode::WatchdogReset)
        d.add(ItlIbss::ExperimentObservation::RecoveryFaults);
    d.add(ItlIbss::ExperimentObservation::LifecycleEvents);
    if(!gated)d.add(ItlIbss::ExperimentObservation::LifecycleOffGate);
    d.control.append(ItlIbss::lifecycleRow(code,now,uint32_t(com.sc_generation),phase,
        uint32_t(com.sc_ic.ic_if.if_flags),uint32_t(com.sc_flags),uint32_t(com.sc_ic.ic_state),
        uint32_t(com.ns_nstate),context,detail,d.lifecycleThreadTag(reinterpret_cast<uintptr_t>(current_thread()))));
}

// Called once during attach, before ordinary enable/init. No timer or radio action.
void ItlIwm::awdlIbssPassiveTraceStart() {
    if(awdlProbeCounters)awdlProbeCounters->ibss.armLifecycle(itlAwdlRocNowNs(),240000000000ull);
}

// Called only with a stack copy of the error table already read by iwm_nic_error.
// Reuses the fixed ring; no new hardware read, allocation, timer or command.
void ItlIwm::awdlIbssFirmwareFault(uint32_t core,ItlIbss::FaultRead status,const ItlIbss::FaultSnapshot &snapshot) {
    auto counters=__atomic_load_n(&awdlProbeCounters,__ATOMIC_SEQ_CST);if(!counters)return;
    auto &d=counters->ibss;const auto now=itlAwdlRocNowNs();
    if(!d.lifecycleCaptures(now))return;
    const auto phase=getMainWorkLoop()->inGate()?uint32_t(awdlIbssExclusive.phase):d.get(ItlIbss::ExperimentObservation::Phase);
    d.control.append(ItlIbss::firmwareFaultRow(core,status,snapshot,now,uint32_t(com.sc_generation),phase));
}
