// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "IbssExclusive.hpp"
// This backend bootstraps an exclusive firmware session, not the IBSS PHY/MAC.
// Default-off one-shot entry is installed separately. Poll is inert in Idle.
struct ItlIbssExclusiveTransport {
    ItlIwm *hal;
    uint64_t now() const { return itlAwdlRocNowNs(); }
    uint32_t generation() const { return uint32_t(hal->com.sc_generation); }
    bool administrativelyUp() const {
        return !hal->dmaDetaching && (hal->com.sc_ic.ic_if.if_flags & IFF_UP) &&
            !(hal->com.sc_flags & (IWM_FLAG_RFKILL | IWM_FLAG_SHUTDOWN));
    }
    bool canBegin() const {
        auto sc = &hal->com;
        const auto channel=&sc->sc_ic.ic_channels[6];
        const bool profile=sc->sc_nvm.sku_cap_11n_enable && channel->ic_freq==2437 &&
            (channel->ic_flags & IEEE80211_CHAN_2GHZ) && (channel->ic_flags & IEEE80211_CHAN_HT20) &&
            !(channel->ic_flags & IEEE80211_CHAN_PASSIVE) &&
            isset(sc->sc_enabled_capa,IWM_UCODE_TLV_CAPA_DYNAMIC_QUOTA) &&
            isset(sc->sc_enabled_capa,IWM_UCODE_TLV_CAPA_DQA_SUPPORT) &&
            isset(sc->sc_ucode_api,IWM_UCODE_TLV_API_STA_TYPE) &&
            isset(sc->sc_ucode_api,34) && isset(sc->sc_ucode_api,46) &&
            isset(sc->sc_enabled_capa,70) && isset(sc->sc_enabled_capa,71);
        return profile && hal->getMainWorkLoop()->inGate() && !hal->dmaDetaching &&
            sc->sc_pcitag && sc->sc_ic.ic_opmode == IEEE80211_M_STA &&
            sc->sc_ic.ic_state == IEEE80211_S_RUN && sc->ns_nstate == IEEE80211_S_RUN &&
            (sc->sc_ic.ic_if.if_flags & IFF_RUNNING) && !hal->awdlScanSubmitting &&
            !hal->awdlAuxTx.pending && !ItlAwdl::gEngine.running &&
            hal->awdlRocSession.phase == ItlAwdl::RocPhase::Idle &&
            !(sc->sc_flags & (IWM_FLAG_HW_ERR | IWM_FLAG_SCANNING | IWM_FLAG_BGSCAN |
                             IWM_FLAG_TE_ACTIVE));
    }
    bool clearStoppedAggregation(uint32_t previous) {
        auto &s=hal->awdlIbssExclusive;auto sc=&hal->com;
        if(!hal->getMainWorkLoop()->inGate() || hal->getMainWorkLoop()->onThread() ||
           !s.ownsRadio || !s.operation || s.phase!=ItlIbss::ExclusivePhase::Quiescing ||
           !stoppedProof(previous))return false;
        // Same host bookkeeping as ordinary iwm_init, after real DMA/RX stop.
        hal->awdlIbssExperimentEvent(42,1,0,sc->agg_queue_mask,sc->agg_tid_disable);
        sc->agg_tid_disable=0xffff;sc->agg_queue_mask=0;
        memset(sc->sc_tx_ba,0,sizeof(sc->sc_tx_ba));
        hal->awdlIbssExperimentEvent(42,2,0,sc->agg_queue_mask,sc->agg_tid_disable);
        return true;
    }
    void stop() {
        const uint32_t previous=generation();
        if (!hal->dmaDetaching) hal->awdlIbssInitStep(0x40001,[&]{hal->iwm_stop(&hal->com.sc_ic.ic_if);return 0;});
        if(hal->awdlIbssExclusive.phase==ItlIbss::ExclusivePhase::Quiescing && !clearStoppedAggregation(previous))
            hal->awdlIbssExclusive.resetRequested=true;
    }
    bool stoppedProof(uint32_t previous) const {
        auto sc = &hal->com;
        if (hal->dmaDetaching || !sc->sc_pcitag || generation() == previous ||
            (sc->sc_pcitag->configRead16(kIOPCIConfigCommand) & kIOPCICommandBusMaster) ||
            sc->sc_ic.ic_state != IEEE80211_S_INIT || sc->ns_nstate != IEEE80211_S_INIT ||
            (sc->sc_ic.ic_if.if_flags & IFF_RUNNING) ||
            (sc->sc_flags & (IWM_FLAG_MAC_ACTIVE | IWM_FLAG_BINDING_ACTIVE |
                             IWM_FLAG_STA_ACTIVE | IWM_FLAG_TE_ACTIVE))) return false;
        for (unsigned q = 0; q < IWM_MAX_QUEUES; ++q)
            if (sc->txq[q].queued || sc->txq[q].cur != sc->txq[q].tail) return false;
        return true;
    }
    int startExclusive() {
        if (!administrativelyUp() || hal->awdlIbssExclusive.resetRequested) return ECANCELED;
        ItlIbss::CommandScope authority(hal->awdlIbssExclusive.commandAuthority,
                                       reinterpret_cast<uintptr_t>(current_thread()));
        if (!authority) return EBUSY;
        // Unlike iwm_init(), this DOES NOT start ordinary scanning/association.
        return hal->awdlIbssInitStep(0x40002,[&]{return hal->iwm_init_hw(&hal->com);});
    }
    bool exclusiveFirmwareProof() const {
        auto sc = &hal->com;
        return !hal->awdlIbssExclusive.resetRequested && !hal->dmaDetaching && sc->sc_uc.uc_intr && sc->sc_uc.uc_ok &&
            sc->sc_uc_current == IWM_UCODE_TYPE_REGULAR &&
            sc->sc_ic.ic_state == IEEE80211_S_INIT && sc->ns_nstate == IEEE80211_S_INIT &&
            !(sc->sc_flags & (IWM_FLAG_MAC_ACTIVE | IWM_FLAG_BINDING_ACTIVE |
                IWM_FLAG_STA_ACTIVE | IWM_FLAG_SCANNING | IWM_FLAG_BGSCAN | IWM_FLAG_HW_ERR));
    }
    bool reserveFresh(ItlIbss::IwmSession &session) const {
        // Only the fresh iwm_init_hw image is inventoried here: its AUX STA1,
        // queues0/1/5..8 and three precreated PHY contexts. This is NOT a
        // generic allocator for a firmware image with existing peer contexts.
        auto sc = &hal->com;
        if (!exclusiveFirmwareProof() ||
            !isset(sc->sc_enabled_capa,IWM_UCODE_TLV_CAPA_DQA_SUPPORT) ||
            !isset(sc->sc_ucode_api,IWM_UCODE_TLV_API_STA_TYPE)) return false;
        for (unsigned i = 0; i < 2; ++i) {
            auto ring = &sc->txq[i ? 9 : 3];
            if (!ring->desc || !ring->cmd || ring->queued || ring->cur != ring->tail) return false;
        }
        session = ItlIbss::IwmSession();
        session.resources = {1,1,1,3,4,3,9,true};
        session.generation = generation();
        session.phyColor = uint8_t(sc->sc_phyctxt[1].color);
        // No PHY/profile proof yet. Command backend MUST remain closed.
        session.prerequisitesConfirmed = false;
        return true;
    }
    int startSta() {
        if (!administrativelyUp()) return ECANCELED;
        return hal->awdlIbssInitStep(0x40003,[&]{return hal->iwm_init(&hal->com.sc_ic.ic_if);});
    }
    bool associatedAndAuthorized() const {
        auto ic = &hal->com.sc_ic;
        return !hal->dmaDetaching && ic->ic_state == IEEE80211_S_RUN &&
            hal->com.ns_nstate == IEEE80211_S_RUN && (ic->ic_if.if_flags & IFF_RUNNING) &&
            ic->ic_bss && (!(ic->ic_flags & IEEE80211_F_RSNON) || ic->ic_bss->ni_port_valid);
    }
};
int ItlIwm::awdlIbssExclusiveBegin(uint64_t durationNs) {
    ItlAwdlRocLifetime lifetime(this,getMainCommandGate(),getMainWorkLoop());
    if (!getMainWorkLoop()->inGate() || getMainWorkLoop()->onThread()) return EBUSY;
    ItlIbssExclusiveTransport transport{this};
    ItlIbss::Exclusive<ItlIbssExclusiveTransport> executor(transport,awdlIbssExclusive);
    const bool nativeSearch = awdlIbssWorkPending && awdlNativeSearch.active() &&
        awdlNativeSearch.matches(awdlIbssWorkerEpoch);
    return executor.begin(transport.now(),durationNs,nativeSearch);
}
int ItlIwm::awdlIbssExclusiveEnd() {
    ItlAwdlRocLifetime lifetime(this,getMainCommandGate(),getMainWorkLoop());
    if (!getMainWorkLoop()->inGate() || getMainWorkLoop()->onThread()) return EBUSY;
    awdlNativeControlTxStop(1);
    awdlIbssTxCancel();
    awdlIbssPeerCancel();
    ItlIbssExclusiveTransport transport{this};
    ItlIbss::Exclusive<ItlIbssExclusiveTransport> executor(transport,awdlIbssExclusive);
    return executor.end(transport.now());
}
int ItlIwm::awdlIbssExclusivePoll() {
    if (awdlIbssExclusive.phase == ItlIbss::ExclusivePhase::Idle) return 0;
    ItlAwdlRocLifetime lifetime(this,getMainCommandGate(),getMainWorkLoop());
    if (!getMainWorkLoop()->inGate()) {
        struct Call { ItlIwm *hal; int result; } call{this,EBUSY};
        getMainCommandGate()->runAction([](OSObject *, void *p, void *, void *, void *) -> IOReturn {
            auto c = static_cast<Call *>(p); c->result = c->hal->awdlIbssExclusivePoll();
            return kIOReturnSuccess;
        },&call);
        return call.result;
    }
    if (getMainWorkLoop()->onThread()) return EBUSY;
    ItlIbssExclusiveTransport transport{this};
    ItlIbss::Exclusive<ItlIbssExclusiveTransport> executor(transport,awdlIbssExclusive);
    auto &state=awdlIbssExclusive;
    const bool watching=state.phase==ItlIbss::ExclusivePhase::AwaitAssociation;
    const uint32_t expected=state.generation,actual=transport.generation();
    const uint32_t reason=(transport.administrativelyUp()?0u:1u)|(actual!=expected?2u:0u);
    int error=executor.poll(transport.now());
    // Only the radio-released observation phase may accept ordinary reinit epochs.
    if(watching && actual!=expected && state.phase!=ItlIbss::ExclusivePhase::AwaitAssociation)
        awdlIbssExperimentEvent(44,actual,error,expected,uint32_t(state.phase));
    if(watching && state.phase==ItlIbss::ExclusivePhase::Cancelled) {
        if(awdlProbeCounters) {
            auto &d=awdlProbeCounters->ibss;using D=ItlIbss::ExperimentObservation;
            d.set(D::CancelExpectedGeneration,expected);d.set(D::CancelActualGeneration,actual);d.set(D::CancelReason,reason);
        }
        awdlIbssLifecycleSource(ItlIbss::LifeCode::RecoveryCancelled,expected);
    }
    return error;
}
