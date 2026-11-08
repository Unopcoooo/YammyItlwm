// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "IbssProfile.hpp"
#include "Channel6Policy.hpp"
#include "Channel6Permission.hpp"
static_assert(sizeof(iwm_phy_context_cmd) == 36 && sizeof(iwm_phy_context_cmd_uhb) == 40,
              "PHY short/extended command layouts");
int ItlIwm::awdlIbssCandidateConfigure() {
    ItlAwdlRocLifetime lifetime(this,getMainCommandGate(),getMainWorkLoop());
    if(awdlNativeSearchReserved()&&!awdlNativeSearchMayConfigure())return ECANCELED;
    auto &state = awdlIbssExclusive; auto &session = state.session; auto sc = &com;
    if (!getMainWorkLoop()->inGate() || getMainWorkLoop()->onThread() || dmaDetaching || !state.ownsRadio || state.operation ||
        state.phase != ItlIbss::ExclusivePhase::FirmwareReady || session.prerequisitesConfirmed ||
        session.needsReset || state.resetRequested || uint32_t(sc->sc_generation) != session.generation ||
        itlAwdlRocNowNs() >= state.deadline || !session.resources.valid()) return EBUSY;
    static_assert(ItlIbss::Channel6NativeRequired==(IEEE80211_CHAN_2GHZ|IEEE80211_CHAN_HT20),"channel6 native eligibility ABI");
    static_assert(ItlIbss::Channel6NativeDenied==(IEEE80211_CHAN_PASSIVE|IEEE80211_CHAN_DFS|IEEE80211_CHAN_NOADHOC),"channel6 native restriction ABI");
    static_assert(ItlIbss::Profile5Required==(IEEE80211_CHAN_5GHZ|IEEE80211_CHAN_HT20),"channel eligibility ABI");
    static_assert(ItlIbss::Profile5Denied==(IEEE80211_CHAN_PASSIVE|IEEE80211_CHAN_DFS|IEEE80211_CHAN_NOADHOC),"channel restriction ABI");
    static_assert(ItlIbss::ProfileNvmRequired==(IWM_NVM_CHANNEL_VALID|IWM_NVM_CHANNEL_IBSS|IWM_NVM_CHANNEL_ACTIVE),"NVM eligibility ABI");
    static_assert(ItlIbss::ProfileNvmDenied==(IWM_NVM_CHANNEL_RADAR|IWM_NVM_CHANNEL_DFS),"NVM restriction ABI");
    if(!awdlProbeCounters)return ENOTSUP;
    using D=ItlIbss::ExperimentObservation;auto &d=awdlProbeCounters->ibss;
    auto ic=&sc->sc_ic;
    struct Operation {
        bool &flag;
        explicit Operation(bool &f) : flag(f) { flag = true; }
        ~Operation() { flag = false; }
    } operation(state.operation);
    ItlIbss::CommandScope authority(state.commandAuthority,
                                   reinterpret_cast<uintptr_t>(current_thread()));
    if (!authority) return EBUSY;
    struct PermissionTransport {
        ItlIwm *hal;uint32_t generation;
        bool healthy() const {
            const auto &s=hal->awdlIbssExclusive;const auto sc=&hal->com;
            return hal->awdlNativeSearchMayConfigure() && hal->getMainWorkLoop()->inGate() && !hal->getMainWorkLoop()->onThread() &&
                !hal->dmaDetaching && s.ownsRadio && !s.resetRequested &&
                s.phase==ItlIbss::ExclusivePhase::FirmwareReady &&
                uint32_t(sc->sc_generation)==generation && itlAwdlRocNowNs()<s.deadline &&
                (sc->sc_ic.ic_if.if_flags&IFF_UP) && sc->sc_ic.ic_state==IEEE80211_S_INIT &&
                sc->ns_nstate==IEEE80211_S_INIT &&
                !(sc->sc_flags&(IWM_FLAG_HW_ERR|IWM_FLAG_RFKILL|IWM_FLAG_SHUTDOWN|
                    IWM_FLAG_SCANNING|IWM_FLAG_BGSCAN|IWM_FLAG_MAC_ACTIVE|IWM_FLAG_BINDING_ACTIVE|IWM_FLAG_STA_ACTIVE|IWM_FLAG_TE_ACTIVE));
        }
        bool permissionKnown() const {
            using D=ItlIbss::ExperimentObservation;const auto &d=hal->awdlProbeCounters->ibss;
            return d.get(D::MccProfileVersion)==1 && d.get(D::MccDecoded) && d.get(D::MccGeneration)==generation;
        }
        bool canGetCurrent() const {
            auto sc=&hal->com;
            return isset(sc->sc_ucode_api,IWM_UCODE_TLV_API_WIFI_MCC_UPDATE) ||
                isset(sc->sc_enabled_capa,IWM_UCODE_TLV_CAPA_LAR_MULTI_MCC);
        }
        int getCurrent() {
            if(!healthy() || !canGetCurrent())return ENOTSUP;
            return hal->awdlIbssInitStep(0x30040,[&]{return hal->iwm_send_update_mcc_cmd(&hal->com,"ZZ");});
        }
        bool allowed() const {
            using D=ItlIbss::ExperimentObservation;const auto &d=hal->awdlProbeCounters->ibss;
            ItlIbss::MccChannelProfile m;m.decoded=permissionKnown();m.generation=d.get(D::MccGeneration);
            m.status=d.get(D::MccStatus);m.present=uint8_t(d.get(D::MccPresent));m.flags6=d.get(D::MccFlags6);
            const auto c=&hal->com.sc_ic.ic_channels[6];
            return healthy() && ItlIbss::channel6ProfileAllowed(6,c->ic_freq,c->ic_flags,d.get(D::NvmFlags6),
                d.get(D::NvmChannelFlagsVersion)==1,m,generation);
        }
    } permission{this,uint32_t(sc->sc_generation)};
    ItlIbss::Channel6QueryBudget queryBudget;
    const int permissionError=ItlIbss::acquireChannel6Permission(permission,queryBudget);
    awdlIbssExperimentEvent(21,6,permissionError,d.get(D::MccFlags6),d.get(D::MccGeneration));
    if(permissionError)return permissionError;
    const unsigned targetChannel=6;
    d.set(D::TargetChannel,targetChannel);
    if(!sc->sc_nvm.sku_cap_11n_enable ||
        !isset(sc->sc_enabled_capa,IWM_UCODE_TLV_CAPA_DYNAMIC_QUOTA) ||
        !isset(sc->sc_enabled_capa,70) || !isset(sc->sc_enabled_capa,71) ||
        !isset(sc->sc_ucode_api,34))return ENOTSUP;
    auto &mac = session.mac;
    mac.channel = targetChannel;
    mac.id = session.resources.mac; mac.tsf = session.resources.tsf; mac.color = 0;
    const uint8_t bssid[6] = {0,0x25,0,0xff,0x94,0x73};
    if(!awdlIbssLocalAddress(mac.source))return EINVAL;
    memcpy(mac.bssid,bssid,6);
    mac.beaconIntervalTu = 10000; mac.cckAckRates = 15; mac.ofdmAckRates = 0x15;
    mac.shortPreamble = true; mac.shortSlot = mac.qos = true;
    mac.ac[0] = {15,1023,7,1,0}; mac.ac[1] = {15,1023,3,2,0};
    mac.ac[2] = {7,15,2,4,3008}; mac.ac[3] = {3,7,2,8,1504};
    uint8_t beacon[146];size_t length=0;
    if(awdlListenerActive()){
        if(!awdlListenerReserveQueue())return EBUSY;
    }else{
    iwm_setup_ht_rates(sc);
    ItlIbss::BeaconCapabilities caps = {};
    caps.htCaps = ic->ic_htcaps; caps.ampdu = ic->ic_ampdu_params; caps.txMcs = ic->ic_tx_mcs_set;
    for (unsigned i = 0; i < 10; ++i) caps.mcs[i] = ic->ic_sup_mcs[i];
    if (!ItlIbss::buildBeacon20(mac,caps,beacon,sizeof(beacon),length)) return EINVAL;
    }
    struct Transport {
        ItlIwm *hal;
        bool uhb() const { return isset(hal->com.sc_enabled_capa,IWM_UCODE_TLV_CAPA_ULTRA_HB_CHANNELS); }
        int phy(const void *data, size_t n) {
            auto &state = hal->awdlIbssExclusive; auto sc = &hal->com;
            if (hal->dmaDetaching || state.resetRequested || uint32_t(sc->sc_generation) != state.generation ||
                !(sc->sc_ic.ic_if.if_flags & IFF_UP) ||
                (sc->sc_flags & (IWM_FLAG_HW_ERR | IWM_FLAG_RFKILL | IWM_FLAG_SHUTDOWN)) ||
                itlAwdlRocNowNs() >= state.deadline || sc->sc_ic.ic_state != IEEE80211_S_INIT ||
                sc->ns_nstate != IEEE80211_S_INIT) return EBUSY;
            using D=ItlIbss::ExperimentObservation;using H=ItlIbss::ExperimentRows;
            // Match native/official CDB cross-band REMOVE then ADD. Initial
            // contexts exist on channel1; no MAC/binding has been added yet.
            auto &ctx=sc->sc_phyctxt[state.session.resources.phy];
            if(!ctx.channel)return EINVAL;
            const bool crossBand=bool(ctx.channel->ic_flags & IEEE80211_CHAN_2GHZ) != (state.session.mac.channel==6);
            const bool replace=crossBand && isset(sc->sc_enabled_capa,IWM_UCODE_TLV_CAPA_BINDING_CDB_SUPPORT);
            uint8_t cmd[sizeof(iwm_phy_context_cmd_uhb)];
            if(!data || (n!=sizeof(iwm_phy_context_cmd) && n!=sizeof(iwm_phy_context_cmd_uhb)))return EINVAL;
            for(size_t i=0;i<n;++i)cmd[i]=static_cast<const uint8_t*>(data)[i];
            auto healthy=[&](){return !hal->dmaDetaching && !state.resetRequested && state.ownsRadio &&
                uint32_t(sc->sc_generation)==state.generation && itlAwdlRocNowNs()<state.deadline &&
                (sc->sc_ic.ic_if.if_flags & IFF_UP) &&
                !(sc->sc_flags & (IWM_FLAG_HW_ERR|IWM_FLAG_RFKILL|IWM_FLAG_SHUTDOWN)) &&
                sc->sc_ic.ic_state==IEEE80211_S_INIT && sc->ns_nstate==IEEE80211_S_INIT;};
            if(!healthy())return EBUSY;
            if(hal->awdlProbeCounters)hal->awdlProbeCounters->ibss.add(D::CommandSubmitted);
            hal->awdlIbssExperimentEvent(H::CommandSubmit,IWM_PHY_CONTEXT_CMD,0,replace?IWM_FW_CTXT_ACTION_REMOVE:IWM_FW_CTXT_ACTION_MODIFY,0);
            int error=0;
            if(replace) {
                ItlIbss::le32(cmd+4,IWM_FW_CTXT_ACTION_REMOVE);
                error=hal->iwm_send_cmd_pdu(sc,IWM_PHY_CONTEXT_CMD,0,uint16_t(n),cmd);
                if(hal->awdlProbeCounters) {
                    hal->awdlProbeCounters->ibss.add(D::CommandReturned);
                    if(error)hal->awdlProbeCounters->ibss.add(D::CommandErrors);
                    hal->awdlIbssExperimentEvent(H::CommandReturn,IWM_PHY_CONTEXT_CMD,error,IWM_FW_CTXT_ACTION_REMOVE,0);
                }
                if(error || !healthy())return error?error:EIO;
                ItlIbss::le32(cmd+4,IWM_FW_CTXT_ACTION_ADD);
                if(hal->awdlProbeCounters)hal->awdlProbeCounters->ibss.add(D::CommandSubmitted);
                hal->awdlIbssExperimentEvent(H::CommandSubmit,IWM_PHY_CONTEXT_CMD,0,IWM_FW_CTXT_ACTION_ADD,0);
            }
            error = hal->iwm_send_cmd_pdu(sc,IWM_PHY_CONTEXT_CMD,0,uint16_t(n),cmd);
            if(hal->awdlProbeCounters) {
                hal->awdlProbeCounters->ibss.add(D::CommandReturned);
                if(error)hal->awdlProbeCounters->ibss.add(D::CommandErrors);
                hal->awdlIbssExperimentEvent(H::CommandReturn,IWM_PHY_CONTEXT_CMD,error,replace?IWM_FW_CTXT_ACTION_ADD:IWM_FW_CTXT_ACTION_MODIFY,0);
            }
            if(error || !healthy())return error?error:EIO;
            sc->sc_phyctxt[state.session.resources.phy].channel = &sc->sc_ic.ic_channels[state.session.mac.channel];
            return 0;
        }
        int command(ItlIbss::Action a, const uint8_t *b, size_t n) {
            if(hal->awdlIbssExclusive.resetRequested)return ECANCELED;
            return hal->awdlIbssCandidateCommand(hal->awdlIbssExclusive.session,a,b,n);
        }
    } transport{this};
    const uint8_t rx = iwm_fw_valid_rx_ant(sc), tx = iwm_fw_valid_tx_ant(sc);
    const bool listener=awdlListenerActive();
    int error=listener?ItlIbss::configureListenerProfile(transport,session,rx,tx,iwm_mimo_enabled(sc)?2:1):
        ItlIbss::configureProfile(transport,session,beacon,length,rx,tx,iwm_mimo_enabled(sc)?2:1);
    if(listener)awdlIbssExperimentEvent(51,1,error,IWM_FW_MAC_TYPE_LISTENER,0x1847);
    if(awdlIbssReceiveOnlyActive())awdlIbssExperimentEvent(52,1,error,IWM_FW_MAC_TYPE_IBSS,0x1045);
    if (error) session.needsReset = true;
    return error; // Watchdog owns bounded full reset/recovery on failure/expiry.
}
