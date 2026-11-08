// SPDX-License-Identifier: GPL-2.0-or-later
// Included only by the isolated candidate mac80211.cpp, under AIRPORT_WCL.
#pragma once
#include "IbssIwmCommands.hpp"
static_assert(sizeof(iwm_mac_ctx_cmd) == ItlIbss::MacBytes, "IBSS MAC ABI");
static_assert(IWM_DQA_GCAST_QUEUE == 3 && IWM_DQA_AP_PROBE_RESP_QUEUE == 9,
              "IBSS role queue contract");
static_assert(IWM_DQA_INJECT_MONITOR_QUEUE==2 && IWM_MAX_TID_COUNT==8 && IWM_FW_MAC_TYPE_LISTENER==2 && IWM_STA_GENERAL_PURPOSE==1,"listener native ABI");
static_assert(IWM_NUM_PHY_CTX == 3 && IWM_STATION_COUNT == 16, "resource bounds");

int ItlIwm::awdlIbssCandidateCommand(ItlIbss::IwmSession &session,
                                    ItlIbss::Action action,
                                    const uint8_t *frame, size_t length) {
#ifndef AIRPORT_VTD
    return ENOTSUP;
#else
    if (!awdlIbssExclusive.ownsRadio ||
        awdlIbssExclusive.phase != ItlIbss::ExclusivePhase::FirmwareReady ||
        &session != &awdlIbssExclusive.session) return EBUSY;
    if(!ItlIbss::profileActionAllowed(action,awdlListenerActive()))return EBUSY;
    const bool creating=awdlNativeSearchReserved()&&
        ((action>=ItlIbss::Action::ConfigurePhy&&action<=ItlIbss::Action::GrantQuota)||action>=ItlIbss::Action::AddListenerMac);
    if(creating&&!awdlNativeSearchMayConfigure())return ECANCELED;
    ItlIbss::CommandScope authority(awdlIbssExclusive.commandAuthority,
                                   reinterpret_cast<uintptr_t>(current_thread()));
    if (!authority) return EBUSY;
    // Only the bounded experiment profile invokes this backend.
    struct Transport {
        ItlIwm *hal;
        ItlIbss::IwmSession &session;
        bool creating;
        bool ready(const ItlIbss::IwmSession &s) const {
            auto sc = &hal->com;
            if ((creating&&!hal->awdlNativeSearchMayConfigure()) || !s.valid() || hal->awdlIbssExclusive.resetRequested || !hal->getMainWorkLoop()->inGate() || hal->dmaDetaching ||
                uint32_t(sc->sc_generation) != s.generation || hal->awdlScanSubmitting ||
                !(sc->sc_ic.ic_if.if_flags & IFF_UP) ||
                itlAwdlRocNowNs() >= hal->awdlIbssExclusive.deadline ||
                ItlAwdl::gEngine.running || sc->sc_ic.ic_state != IEEE80211_S_INIT ||
                sc->ns_nstate != IEEE80211_S_INIT ||
                (sc->sc_flags & (IWM_FLAG_SHUTDOWN | IWM_FLAG_HW_ERR | IWM_FLAG_RFKILL |
                    IWM_FLAG_SCANNING | IWM_FLAG_BGSCAN | IWM_FLAG_MAC_ACTIVE |
                    IWM_FLAG_BINDING_ACTIVE | IWM_FLAG_STA_ACTIVE | IWM_FLAG_TE_ACTIVE)))
                return false;
            if (!isset(sc->sc_enabled_capa,IWM_UCODE_TLV_CAPA_DQA_SUPPORT) ||
                !isset(sc->sc_ucode_api,IWM_UCODE_TLV_API_STA_TYPE) ||
                !isset(sc->sc_ucode_api,34) || !isset(sc->sc_enabled_capa,70) ||
                !isset(sc->sc_enabled_capa,71)) return false;
            auto phy = &sc->sc_phyctxt[s.resources.phy];
            if (phy->id != s.resources.phy || phy->color != s.phyColor || !phy->channel ||
                ieee80211_chan2ieee(&sc->sc_ic,phy->channel) != s.mac.channel ||
                phy->channel->ic_freq != (s.mac.channel==6?2437:5000+s.mac.channel*5) ||
                !(phy->channel->ic_flags & (s.mac.channel==6?IEEE80211_CHAN_2GHZ:IEEE80211_CHAN_5GHZ)) ||
                !(phy->channel->ic_flags & IEEE80211_CHAN_HT20)) return false;
            for (unsigned i = 0; i < 2; ++i) {
                const unsigned qid = i ? 9 : 3;
                auto ring = &sc->txq[qid];
                if (!ring->desc || !ring->cmd || ring->queued || ring->cur != ring->tail)
                    return false;
            }
            if(hal->awdlListenerActive()&&!hal->awdlListenerQueueReady())return false;
            return sc->sched_dma.vaddr != nullptr;
        }
        int checked(int error) const {
            if (uint32_t(hal->com.sc_generation) != session.generation) return EIO;
            return error;
        }
        int submitPdu(uint32_t id, const void *data, size_t n,uint32_t flags) {
            if (!ready(session)) return EBUSY;
            hal->awdlIbssExperimentEvent(ItlIbss::ExperimentRows::CommandSubmit,id,0,flags,0);
            if(hal->awdlProbeCounters)hal->awdlProbeCounters->ibss.add(ItlIbss::ExperimentObservation::CommandSubmitted);
            int error=checked(hal->iwm_send_cmd_pdu(&hal->com,id,flags,uint16_t(n),data));
            if(hal->awdlProbeCounters) {
                hal->awdlProbeCounters->ibss.add(ItlIbss::ExperimentObservation::CommandReturned);
                if(error)hal->awdlProbeCounters->ibss.add(ItlIbss::ExperimentObservation::CommandErrors);
                hal->awdlIbssExperimentEvent(ItlIbss::ExperimentRows::CommandReturn,id,error,flags,0);
            }
            return error;
        }
        int pdu(uint32_t id,const void *data,size_t n) {return submitPdu(id,data,n,0);}
        int asyncPdu(uint32_t id,const void *data,size_t n) {
            if(id!=IWM_BEACON_TEMPLATE_CMD)return EINVAL;
            return submitPdu(id,data,n,IWM_CMD_ASYNC);
        }
        int status(uint32_t id, const void *data, size_t n, uint32_t &result) {
            if (!ready(session)) return EBUSY;
            hal->awdlIbssExperimentEvent(ItlIbss::ExperimentRows::CommandSubmit,id,0,0,0);
            if(hal->awdlProbeCounters)hal->awdlProbeCounters->ibss.add(ItlIbss::ExperimentObservation::CommandSubmitted);
            int error=checked(hal->iwm_send_cmd_pdu_status(&hal->com,id,uint16_t(n),data,&result));
            if(hal->awdlProbeCounters) {
                hal->awdlProbeCounters->ibss.add(ItlIbss::ExperimentObservation::CommandReturned);
                if(error)hal->awdlProbeCounters->ibss.add(ItlIbss::ExperimentObservation::CommandErrors);
                hal->awdlIbssExperimentEvent(ItlIbss::ExperimentRows::FirmwareStatus,id,error,!error,result);
            }
            return error;
        }
        size_t bindingBytes() const {
            return isset(hal->com.sc_enabled_capa,IWM_UCODE_TLV_CAPA_BINDING_CDB_SUPPORT)
                ? sizeof(struct iwm_binding_cmd) : sizeof(struct iwm_binding_cmd_v1);
        }
        bool dynamicQuota() const {
            return isset(hal->com.sc_enabled_capa,IWM_UCODE_TLV_CAPA_DYNAMIC_QUOTA);
        }
        int enableQueue(uint8_t station, uint8_t queue, uint8_t fifo, uint8_t tid) {
            if(queue==IWM_DQA_INJECT_MONITOR_QUEUE&&(!hal->awdlListenerQueueReady()||station!=session.resources.mcastSta||fifo!=1||tid!=IWM_MAX_TID_COUNT))return EBUSY;
            if (!ready(session) || !hal->iwm_nic_lock(&hal->com)) return EBUSY;
            hal->awdlIbssExperimentEvent(ItlIbss::ExperimentRows::CommandSubmit,IWM_SCD_QUEUE_CFG,0,queue,station);
            if(hal->awdlProbeCounters)hal->awdlProbeCounters->ibss.add(ItlIbss::ExperimentObservation::CommandSubmitted);
            int error = hal->iwm_enable_txq(&hal->com,station,queue,fifo,0,tid,0);
            if(hal->awdlProbeCounters) {
                hal->awdlProbeCounters->ibss.add(ItlIbss::ExperimentObservation::CommandReturned);
                if(error)hal->awdlProbeCounters->ibss.add(ItlIbss::ExperimentObservation::CommandErrors);
                hal->awdlIbssExperimentEvent(ItlIbss::ExperimentRows::CommandReturn,IWM_SCD_QUEUE_CFG,error,queue,station);
            }
            hal->iwm_nic_unlock(&hal->com);
            return checked(error);
        }
        int disableQueue(uint8_t station, uint8_t queue, uint8_t tid) {
            // Existing iwm_disable_txq hardcodes STA ID 0; do not reuse it.
            iwm_scd_txq_cfg_cmd cmd = {};
            cmd.scd_queue = queue; cmd.sta_id = station; cmd.tid = tid;
            cmd.enable = IWM_SCD_CFG_DISABLE_QUEUE;
            return pdu(IWM_SCD_QUEUE_CFG,&cmd,sizeof(cmd));
        }
        int flush(uint32_t mask) {
            if (!ready(session)) return EBUSY;
            return checked(hal->iwm_flush_tx_path(&hal->com,int(mask)));
        }
    } transport = {this,session,creating};
    // Linux disables Smart FIFO before binding a non-station MAC to the runtime image.
    if(action==ItlIbss::Action::AddBinding && !awdlListenerActive() &&
       session.macAdded && !session.binding && !session.commandInFlight && !session.needsReset) {
        if(!transport.ready(session))return EBUSY;
        const int sfError=transport.checked(iwm_sf_config(&com,IWM_SF_UNINIT));
        // CMD_ASYNC submission only; this is not firmware state readback.
        awdlIbssExperimentEvent(59,1,sfError,IWM_SF_UNINIT,session.generation);
        if(sfError){session.needsReset=true;return sfError;}
    }
    ItlIbss::IwmCommands<Transport> commands(transport,session);
    const bool dataRxDiagnostic=action==ItlIbss::Action::AddMac && (awdlIbssDataActive()||awdlNativeSearchReserved());
    static_assert(IWM_MAC_FILTER_IN_PROMISC==1,"data receive diagnostic flag ABI");
    int error=commands.execute(action,frame,length,dataRxDiagnostic);
    if(dataRxDiagnostic)awdlIbssExperimentEvent(49,1,error,0x1045,session.generation);
    awdlIbssExperimentEvent(ItlIbss::ExperimentRows::State,0x10000u|unsigned(action),error,0,0);
    return error;
#endif
}
