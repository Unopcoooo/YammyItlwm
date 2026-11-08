/*
* Copyright (C) 2020  钟先耀
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*/

#include "ItlIwm.hpp"
#ifdef AIRPORT_WCL
#include <pexpert/pexpert.h>
#include "../awdl/BootOptions.hpp"
#include "../awdl/EngineState.hpp"
#include "../awdl/SyncTemplate.hpp"
#include "../awdl/TemplateRuntime.hpp"

bool ItlIwm::awdlDataContext(ItlAwdl::DataContext &out) {
    out={};out.gateHeld=getMainWorkLoop()&&getMainWorkLoop()->inGate();
    if(!out.gateHeld||!awdlProbeCounters||dmaDetaching)return false;
    if(awdlIbssNativeContext(out))return true;
    auto sc=&com;auto ic=&sc->sc_ic;
    out.associated=ic->ic_state==IEEE80211_S_RUN&&sc->ns_nstate==IEEE80211_S_RUN&&
        !(sc->sc_flags&(IWM_FLAG_SHUTDOWN|IWM_FLAG_HW_ERR|IWM_FLAG_RFKILL|IWM_FLAG_SCANNING|IWM_FLAG_BGSCAN));
    if(!out.associated||!iwm_nic_lock(sc))return false;
    out.gp2=iwm_read_prph(sc,IWM_DEVICE_SYSTEM_TIME_REG);iwm_nic_unlock(sc);
    uint64_t uptime;clock_get_uptime(&uptime);absolutetime_to_nanoseconds(uptime,&out.nowNs);
    out.generation=sc->sc_generation;out.roc=&awdlRocSession;
    out.channel=awdlRocChannel;out.durationUs=awdlRocDurationTu*1024u;
    out.targetGp2=ItlAwdl::gTemplateRadio.targetGp2;out.aw=ItlAwdl::gTemplateRadio.targetAw;
    out.aligned=ItlAwdl::gTemplateRadio.startAligned&&ItlAwdl::gTemplateRadio.targetValid&&
        ItlAwdl::gTemplateRadio.targetGeneration==out.generation;
    out.deadlineNs=ItlAwdl::gEngine.deadlineNs>0?uint64_t(ItlAwdl::gEngine.deadlineNs):0;
    awdlProbeCounters->dataModelView(out);
    // The independent single-frame probe does not service native BSD queues.
    // Keep BSD admission closed until that complete backend is validated.
    out.backendReady=false;
    return true;
}

IOReturn ItlIwm::awdlArmSingleDataProbe(const ItlAwdl::SingleDataRequest &request,const uint8_t local[6]) {
    if(!getMainWorkLoop()||!getMainWorkLoop()->inGate())return kIOReturnNotReady;
    if(awdlIbssDataActive())return kIOReturnNotReady;
    if(ItlAwdl::gSingleDataProbe.everArmed)return kIOReturnNotPermitted;
    uint64_t uptime,now;clock_get_uptime(&uptime);absolutetime_to_nanoseconds(uptime,&now);
    if(!awdlTemplateTxEnabled||!awdlAuxTxEnabled||dmaDetaching||!ItlAwdl::gTemplateRadio.validationArmed||
       !ItlAwdl::gEngine.modeEnabled||!ItlAwdl::gEngine.running||ItlAwdl::gEngine.deadlineNs<=int64_t(now)||
       awdlAuxTx.phase==ItlAwdl::AuxTxPhase::Quarantined||awdlRocSession.phase==ItlAwdl::RocPhase::Quarantined)
        return kIOReturnNotReady;
    ItlAwdl::TemplateSyncConfig sync;ItlAwdl::TemplateService identity;uint32_t revision;
    if(!ItlAwdl::templateSnapshot(sync,identity,revision)||!ItlAwdl::templateIdentityValid(identity,now)||
       memcmp(local,identity.source,6))return kIOReturnNotReady;
    ItlAwdl::SingleDataProbe candidate;
    if(!candidate.arm(request,local,com.sc_generation,now,uint64_t(ItlAwdl::gEngine.deadlineNs)))return kIOReturnBadArgument;
    ItlAwdl::DataContext context;
    if(!awdlDataContext(context)||!candidate.freshPeer(context)){
        ItlAwdl::gSingleDataProbe.blocked(candidate.lastBlockReason?candidate.lastBlockReason:10);
        awdlSingleDataProbePublish();return kIOReturnNotReady;
    }
    ItlAwdl::gSingleDataProbe=candidate;awdlSingleDataProbePublish();return kIOReturnSuccess;
}

// Probe51: single definition of the bounded-engine state, shared with the
// controller layer (see itlwm/awdl/EngineState.hpp).
ItlAwdl::EngineState ItlAwdl::gEngine = {};
ItlAwdl::SyncTemplateState ItlAwdl::gSyncTemplate = {};
ItlAwdl::TemplateRuntimeState ItlAwdl::gTemplateRuntime = {};
ItlAwdl::TemplateRadioState ItlAwdl::gTemplateRadio = {};
ItlAwdl::SingleDataProbe ItlAwdl::gSingleDataProbe = {};
#endif

#define super ItlHalService
OSDefineMetaClassAndStructors(ItlIwm, ItlHalService)
#ifdef AIRPORT_WCL
OSDefineMetaClassAndStructors(ItlAwdlProbeCounters, OSObject)
#endif

void ItlIwm::
detach(IOPCIDevice *device)
{
#ifdef AIRPORT_WCL
    awdlIbssExperimentTimerStop();
    awdlDiscoveryTimerStop();
#endif
#ifdef AIRPORT_VTD
    getMainCommandGate()->runAction(
        [](OSObject *, void *arg0, void *arg1, void *, void *) -> IOReturn {
            auto that = static_cast<ItlIwm *>(arg0);
            auto pci = static_cast<IOPCIDevice *>(arg1);
            that->dmaDetaching = true;
            that->dmaEpoch++;
            that->com.sc_ic.ic_ac.ac_if.if_flags &= ~(IFF_UP | IFF_RUNNING);
            that->com.sc_ic.ic_state = IEEE80211_S_INIT;
            timeout_del(&that->com.sc_ic.ic_bgscan_timeout);
            that->com.sc_flags |= IWM_FLAG_SHUTDOWN;
            that->com.sc_generation++;
            pci->setBusMasterEnable(false);
            auto intr = that->com.ih;
            if (intr && intr->intr)
                intr->intr->disable();
#ifdef AIRPORT_WCL
            if(that->awdlIbssTxPort) {
                that->iwm_disable_interrupts(&that->com);
                that->iwm_reset_rx_ring(&that->com,&that->com.rxq);
                that->awdlIbssTxStopped();
                that->awdlIbssTxDispose();
            }
#endif
            // The peer port can exist without the older management port.
#ifdef AIRPORT_WCL
            if(that->awdlIbssPeerActive()) {
                that->iwm_disable_interrupts(&that->com);
                that->iwm_reset_rx_ring(&that->com,&that->com.rxq);
                that->awdlIbssPeerStopped();
            }
#endif
            for (unsigned int i = 0; i < nitems(that->com.txq); i++)
                that->iwm_reset_tx_ring(&that->com, &that->com.txq[i]);
            return kIOReturnSuccess;
        }, this, device);
#endif
    struct _ifnet *ifp = &com.sc_ic.ic_ac.ac_if;
    struct iwm_softc *sc = &com;
#ifdef AIRPORT_VTD
    releaseAll();
    taskq_destroy(systq);
    taskq_destroy(com.sc_nswq);
#endif
    
    for (int txq_i = 0; txq_i < nitems(sc->txq); txq_i++)
        iwm_free_tx_ring(sc, &sc->txq[txq_i]);
    iwm_rs_free(sc);
    iwm_free_rx_ring(sc, &sc->rxq);
    iwm_dma_contig_free(&sc->ict_dma);
    iwm_dma_contig_free(&sc->kw_dma);
    iwm_dma_contig_free(&sc->sched_dma);
    iwm_dma_contig_free(&sc->fw_dma);
#ifdef AIRPORT_VTD
    iwm_free_fw_paging(sc);
#endif
    ieee80211_ifdetach(ifp);
#ifndef AIRPORT_VTD
    taskq_destroy(systq);
    taskq_destroy(com.sc_nswq);
#endif
    releaseAll();
}

bool ItlIwm::
attach(IOPCIDevice *device)
{
#ifdef AIRPORT_WCL
    int probeArgument = 0;
    awdlProbeEnabled = PE_parse_boot_argn("-itlwm_awdl_probe", &probeArgument, sizeof(probeArgument));
    awdlMgmtFilterEnabled = awdlProbeEnabled &&
        PE_parse_boot_argn("-itlwm_awdl_mgmt", &probeArgument, sizeof(probeArgument));
    int dwellArgument = 0;
    if (awdlProbeEnabled && PE_parse_boot_argn("-itlwm_awdl_dwell", &dwellArgument, sizeof(dwellArgument)) &&
        dwellArgument > 1)
        awdlDwellIterations = (uint32_t)dwellArgument;
    awdlProbeRxSeen = false;
    awdlProbeLogged = 0;
    if (awdlProbeEnabled) {
        awdlProbeCounters = new ItlAwdlProbeCounters;
        bool ready = awdlProbeCounters && awdlProbeCounters->init();
        if (ready) awdlProbeCounters->configureManagementFilter(awdlMgmtFilterEnabled);
        if (!ready || !getController()->setProperty("AWDLRxProbe", awdlProbeCounters)) {
            OSSafeReleaseNULL(awdlProbeCounters);
            awdlProbeEnabled = false;
            awdlMgmtFilterEnabled = false;
        }
    }
    if (awdlProbeEnabled) {
        awdlRocChannel = ItlAwdl::rocChannelOption(PE_parse_boot_argn);
        int peerArgument = 0;
        awdlPeerRocEnabled = awdlRocChannel &&
            PE_parse_boot_argn("-itlwm_awdl_peer_roc_once", &peerArgument, sizeof(peerArgument));
        awdlProbeCounters->setRoc(ItlAwdlProbeCounters::PeerRocEnabled, awdlPeerRocEnabled);
        int txArgument = 0;
        awdlAuxTxEnabled = awdlRocChannel && !awdlPeerRocEnabled &&
            PE_parse_boot_argn("-itlwm_awdl_tx_once", &txArgument, sizeof(txArgument));
        // Probe51: bounded AWDL dwell engine. -itlwm_awdl_engine switches the ROC
        // path from one-shot to a bounded repeating loop; the channel comes from
        // the bare numeric option itlwm_awdl_engine_channel (default 6).
        {
            int engineArgument = 0;
            const bool engineRequested = PE_parse_boot_argn("-itlwm_awdl_engine",
                &engineArgument, sizeof(engineArgument));
            if (engineRequested) {
                int engineChannel = 0;
                if (!PE_parse_boot_argn("itlwm_awdl_engine_channel", &engineChannel, sizeof(engineChannel)) ||
                    (engineChannel != 6 && engineChannel != 44 && engineChannel != 149))
                    engineChannel = 6;
                ItlAwdl::gEngine = {}; // new HAL lifetime, before interrupts
                awdlRocChannel = uint32_t(engineChannel);
                ItlAwdl::gEngine.modeEnabled = 1;
                ItlAwdl::gEngine.channel = engineChannel;
            }
        }
        int templateArgument = 0;
        awdlTemplateTxEnabled = ItlAwdl::gEngine.modeEnabled &&
            PE_parse_boot_argn("-itlwm_awdl_template_tx", &templateArgument, sizeof(templateArgument));
        if (awdlTemplateTxEnabled) awdlAuxTxEnabled = true;
        int peerEngineArgument = 0;
        awdlPeerRocEngine = ItlAwdl::gEngine.modeEnabled &&
            PE_parse_boot_argn("-itlwm_awdl_peer_roc_engine", &peerEngineArgument, sizeof(peerEngineArgument));
        if (awdlPeerRocEngine) {
            awdlPeerRocEnabled = true;
            awdlAuxTxEnabled = false; // this bounded peer experiment is receive-only
        }
        int discoveryArgument=0;
        awdlDiscoveryEnabled=awdlPeerRocEngine &&
            PE_parse_boot_argn("-itlwm_awdl_discovery", &discoveryArgument, sizeof(discoveryArgument));
        int controlRxArgument=0;
        awdlRxControlEnabled=awdlDiscoveryEnabled &&
            PE_parse_boot_argn("-itlwm_awdl_rxcontrol", &controlRxArgument, sizeof(controlRxArgument));
        getController()->setProperty("AWDLRxControlMode",awdlRxControlEnabled);
        awdlProbeCounters->ibss.set(ItlIbss::ExperimentObservation::FirmwareFaultTraceVersion,1);
        awdlProbeCounters->ibss.set(ItlIbss::ExperimentObservation::RecoveryObserverVersion,2);
        int batchArgument=0;
        awdlBatch.enabled=PE_parse_boot_argn("itlwm_awdl_batch",&batchArgument,sizeof(batchArgument)) && batchArgument==1;
        const bool passiveTrace=!awdlBatch.enabled && ItlIbss::bootPassiveTraceRequested(PE_parse_boot_argn);
        awdlProbeCounters->ibss.set(ItlIbss::ExperimentObservation::PassiveTrace,passiveTrace);
        if(passiveTrace)awdlIbssPassiveTraceStart();
        // Passive baseline takes precedence even if stale experiment flags remain.
        awdlIbssOneShot.armed=!awdlBatch.enabled && !passiveTrace && ItlIbss::bootExperimentArmed(PE_parse_boot_argn);
        awdlIbssOneShot.profile=awdlIbssOneShot.armed && ItlIbss::bootProfileRequested(PE_parse_boot_argn);
        awdlProbeCounters->ibss.set(ItlIbss::ExperimentObservation::ProfileRequested,awdlIbssOneShot.profile);
        if(awdlIbssOneShot.armed || awdlBatch.enabled) {
            awdlRocChannel=0; ItlAwdl::gEngine={};
            awdlPeerRocEnabled=false; awdlPeerRocEngine=false;
            awdlDiscoveryEnabled=false; awdlAuxTxEnabled=false;awdlRxControlEnabled=false;
            awdlTemplateTxEnabled=false;
            getController()->setProperty("AWDLRxControlMode",false);
        }
        awdlProbeCounters->ibss.set(ItlIbss::ExperimentObservation::Armed,awdlIbssOneShot.armed);
        int rawRxArgument=0;
        awdlRawRxEnabled=ItlAwdl::rawRxTraceRequested(awdlDiscoveryEnabled,awdlTemplateTxEnabled,
            PE_parse_boot_argn("-itlwm_awdl_rxtrace", &rawRxArgument, sizeof(rawRxArgument)));
        getController()->setProperty("AWDLRawRxTraceRequested",awdlRawRxEnabled);
        awdlProbeCounters->setRoc(ItlAwdlProbeCounters::PeerRocEnabled, awdlPeerRocEnabled);
        awdlProbeCounters->setRoc(ItlAwdlProbeCounters::RocChannel, awdlRocChannel);
        awdlProbeCounters->setRoc(ItlAwdlProbeCounters::TxEnabled, awdlAuxTxEnabled);
        awdlBatchPublish();
        IOLog("itlwm-awdl-probe: v12 enabled; mgmt-filter-requested=%u; dwell-iterations=%u; max 8 frame summaries\n",
              unsigned(awdlMgmtFilterEnabled), unsigned(awdlDwellIterations));
    }
#endif
    pci.pa_tag = device;
    pci.workloop = getMainWorkLoop();
#ifdef AIRPORT_VTD
    if (!prepareDma(device, 36))
        return false;
    pci.pa_dmat = dmaArena;
#endif
    if (!iwm_attach(&com, &pci)) {
        detach(device);
        releaseAll();
        return false;
    }
#ifdef AIRPORT_WCL
    if (awdlDiscoveryEnabled || awdlTemplateTxEnabled) awdlDiscoveryTimerStart();
    if (awdlIbssOneShot.armed) awdlIbssExperimentTimerStart();
#endif
    return true;
}

void ItlIwm::
free()
{
    XYLog("%s\n", __FUNCTION__);
#ifdef AIRPORT_WCL
    OSSafeReleaseNULL(awdlProbeCounters);
#endif
    super::free();
}

void ItlIwm::
releaseAll()
{
#ifdef AIRPORT_WCL
    awdlIbssExperimentTimerStop();
    awdlDiscoveryTimerStop();
#endif
    XYLog("%s\n", __FUNCTION__);
    pci_intr_handle *intrHandler = com.ih;
    if (com.sc_calib_to) {
        timeout_del(&com.sc_calib_to);
        timeout_free(&com.sc_calib_to);
    }
    if (com.sc_led_blink_to) {
        timeout_del(&com.sc_led_blink_to);
        timeout_free(&com.sc_led_blink_to);
    }
    if (intrHandler) {
        if (intrHandler->intr && intrHandler->workloop) {
//            intrHandler->intr->disable();
            intrHandler->workloop->removeEventSource(intrHandler->intr);
            intrHandler->intr->release();
        }
        intrHandler->intr = NULL;
        intrHandler->workloop = NULL;
        intrHandler->arg = NULL;
        intrHandler->dev = NULL;
        intrHandler->func = NULL;
        intrHandler->release();
        com.ih = NULL;
    }
    pci.pa_tag = NULL;
    pci.workloop = NULL;
}

IOReturn ItlIwm::
enable(IONetworkInterface *netif)
{
#ifdef AIRPORT_WCL
    awdlIbssLifecycleSource(ItlIbss::LifeCode::EnableEnter);
#endif
    XYLog("%s\n", __PRETTY_FUNCTION__);
    struct _ifnet *ifp = &com.sc_ic.ic_ac.ac_if;
    if (ifp->if_flags & IFF_UP) {
        XYLog("%s already in activating state\n", __FUNCTION__);
        return kIOReturnSuccess;
    }
    ifp->if_flags |= IFF_UP;
    iwm_activate(&com, DVACT_RESUME);
    iwm_activate(&com, DVACT_WAKEUP);
    return kIOReturnSuccess;
}

IOReturn ItlIwm::
disable(IONetworkInterface *netif)
{
#ifdef AIRPORT_WCL
    awdlIbssLifecycleSource(ItlIbss::LifeCode::DisableEnter);
#endif
    struct _ifnet *ifp = &com.sc_ic.ic_ac.ac_if;
    if (!(ifp->if_flags & IFF_UP)) {
        XYLog("%s already in diactivating state\n", __FUNCTION__);
        return kIOReturnSuccess;
    }
    ifp->if_flags &= ~IFF_UP;
    iwm_activate(&com, DVACT_QUIESCE);
    return kIOReturnSuccess;
}

struct ieee80211com *ItlIwm::
get80211Controller()
{
    return &com.sc_ic;
}

ItlDriverInfo *ItlIwm::
getDriverInfo()
{
    return this;
}

ItlDriverController *ItlIwm::
getDriverController()
{
    return this;
}

void ItlIwm::
clearScanningFlags()
{
    com.sc_flags &= ~(IWM_FLAG_SCANNING | IWM_FLAG_BGSCAN);
}

IOReturn ItlIwm::
setMulticastList(IOEthernetAddress *addr, int count)
{
    struct ieee80211com *ic = &com.sc_ic;
    struct iwm_mcast_filter_cmd *cmd;
    int len;
    uint8_t addr_count;
    int err;
    
    if (ic->ic_state != IEEE80211_S_RUN || ic->ic_bss == NULL)
        return kIOReturnError;
    addr_count = count;
    if (count > IWM_MAX_MCAST_FILTERING_ADDRESSES)
        addr_count = 0;
    if (addr == NULL)
        addr_count = 0;
    len = roundup(sizeof(struct iwm_mcast_filter_cmd) + addr_count * ETHER_ADDR_LEN, 4);
    XYLog("%s multicast count=%d bssid=%s\n", __FUNCTION__, count, ether_sprintf(ic->ic_bss->ni_bssid));
    cmd = (struct iwm_mcast_filter_cmd *)malloc(len, 0, 0);
    if (!cmd)
        return kIOReturnError;
    cmd->pass_all = addr_count == 0;
    cmd->count = addr_count;
    cmd->port_id = 0;
    IEEE80211_ADDR_COPY(cmd->bssid, ic->ic_bss->ni_bssid);
    if (addr_count > 0)
        memcpy(cmd->addr_list,
               addr->bytes, ETHER_ADDR_LEN * cmd->count);
    err = iwm_send_cmd_pdu(&com, IWM_MCAST_FILTER_CMD, IWM_CMD_ASYNC, len,
                     cmd);
    ::free(cmd);
    return err ? kIOReturnError : kIOReturnSuccess;
}

const char *ItlIwm::
getFirmwareVersion()
{
    return com.sc_fwver;
}

const char *ItlIwm::
getFirmwareName()
{
    return com.sc_fwname;
}

UInt32 ItlIwm::
supportedFeatures()
{
    return kIONetworkFeatureMultiPages;
}

const char *ItlIwm::
getFirmwareCountryCode()
{
    return com.sc_fw_mcc;
}

uint32_t ItlIwm::
getTxQueueSize()
{
    return IWM_TX_RING_COUNT;
}

int16_t ItlIwm::
getBSSNoise()
{
    return com.sc_noise;
}

bool ItlIwm::
is5GBandSupport()
{
    return com.sc_nvm.sku_cap_band_52GHz_enable;
}

int ItlIwm::
getTxNSS()
{
    return iwm_mimo_enabled(&com) &&
    (com.sc_ic.ic_bss != NULL && com.sc_ic.ic_bss->ni_rx_nss > 1) ?
    2 : 1;
}
