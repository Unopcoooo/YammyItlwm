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

#ifndef ItlIwm_hpp
#define ItlIwm_hpp

#include "../awdl/NativeSearchOwner.hpp"
#include "../awdl/NativeSearchControlTx.hpp"
#include "../awdl/NativeDiscoverySession.hpp"
#include <compat.h>
#include "itlhdr.h"
#ifdef AIRPORT_WCL
#include "../awdl/ProbeCounters.hpp"
#include "../awdl/AuxRoc.hpp"
#include "../awdl/AuxTx.hpp"
#include "../awdl/SingleDataProbe.hpp"
#include "../awdl/IbssIwmCommands.hpp"
#include "../awdl/IbssExclusive.hpp"
#include "../awdl/IbssLifecycleTrace.hpp"
#include "../awdl/BatchPolicy.hpp"
#include "../awdl/DiscoveryPolicy.hpp"
#include <IOKit/IOTimerEventSource.h>
#endif
#include <linux/kernel.h>

#include <IOKit/IOWorkLoop.h>
#include <IOKit/network/IOGatedOutputQueue.h>
#include <libkern/c++/OSString.h>
#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOLib.h>
#include <libkern/OSKextLib.h>
#include <libkern/c++/OSMetaClass.h>
#include <IOKit/IOFilterInterruptEventSource.h>

#include <HAL/ItlHalService.hpp>
#include <HAL/ItlDriverInfo.hpp>
#include <HAL/ItlDriverController.hpp>

struct ItlIbssTxPort;
struct ItlIbssPeerPort;
namespace ItlIbss {struct PeerGrant;}
class ItlIwm : public ItlHalService, ItlDriverInfo, ItlDriverController {
    OSDeclareDefaultStructors(ItlIwm)
    
public:
#ifdef AIRPORT_WCL
    // Observe only; initialized before interrupts, accessed on the RX workloop.
    ItlAwdlProbeCounters *awdlProbeCounters = nullptr;
    bool awdlDataContext(ItlAwdl::DataContext &out);
    bool awdlIbssNativeContext(ItlAwdl::DataContext &out,bool preparing=false);
    uint32_t awdlIbssNativeLeaseReason();
    int awdlIbssNativeOfferBsd(const uint8_t *,size_t);
    bool awdlIbssNativeOpen(const uint8_t current[6]);
    IOReturn awdlIbssNativeRequest(const ItlAwdl::SingleDataRequest &,const uint8_t current[6]);
    void awdlIbssNativeQueuesStop();
    void awdlIbssNativeWake();
    bool awdlIbssLocalAddress(uint8_t out[6]);
    bool awdlIbssNativeStartIdle();
    bool awdlIbssNativePending();
    void awdlIbssNativeQuiesce();
    bool awdlProbeEnabled = false;
    bool awdlMgmtFilterEnabled = false;
    bool awdlProbeRxSeen = false;
    uint32_t awdlProbeLogged = 0;
    void observeAwdlFrame(mbuf_t frame, int channel, uint32_t receiveGp2, int receiveRssi);
    ItlIbss::ExclusiveState awdlIbssExclusive;
    ItlIbss::OneShot awdlIbssOneShot;
    ItlIbss::BatchControl awdlBatch;
    friend struct ItlIbssTxPort;
    friend struct ItlListenerTxPort;
    ItlIbssTxPort *awdlIbssTxPort=nullptr;
    friend struct ItlIbssPeerPort;
    bool awdlIbssPeerActive();
    bool awdlIbssPeerSubmit();
    IOReturn awdlIbssDataRequest(const ItlAwdl::SingleDataRequest &,bool cancel,unsigned mode=0,bool nativeRequest=false);
    void awdlIbssScheduledTick();
    bool awdlIbssDataActive();
    bool awdlListenerActive();
    bool awdlIbssReceiveOnlyActive();
    IOReturn awdlIbssReceiveOnlyRequest(const ItlAwdl::SingleDataRequest &);
    bool awdlListenerQueueReady();
    bool awdlListenerReserveQueue();
    IOReturn awdlListenerRequest(const ItlAwdl::SingleDataRequest &);
    IOReturn awdlIbssEchoRequest(const ItlAwdl::SingleDataRequest &,unsigned,unsigned);
    bool awdlListenerTxActive();
    bool awdlListenerTxWindowValid();
    void awdlListenerTxTick();
    int awdlListenerTxComplete(unsigned,unsigned,uint32_t);
    void awdlListenerTxResponse(unsigned,unsigned,const void *,size_t);
    void awdlListenerTxCancel();
    void awdlListenerTxStopped();
    IOReturn awdlListenerTxRequest(const ItlAwdl::SingleDataRequest &);
    IOReturn awdlListenerPairRequest(const ItlAwdl::SingleDataRequest &,const uint8_t *,const uint8_t *);
    bool awdlIbssDataMayTransmit(const ItlIbss::PeerGrant &);
    void awdlIbssDataTick();
    void awdlIbssDataWindow();
    void awdlIbssDataPublish();
    void awdlIbssDataPortResult(bool stopped);
    bool awdlIbssPeerPrepare(const uint8_t *,size_t);
    bool awdlIbssPeerTxComplete(unsigned,unsigned,const void *,size_t);
    bool awdlIbssManagementDrained();
    bool awdlIbssIdentityWindowValid();
    int awdlIbssDataIdentity(bool);
    void awdlIbssPeerTick();
    void awdlIbssPeerCancel();
    void awdlIbssPeerStopped();
    int awdlIbssTxProbeOnce();
    bool awdlIbssTxStart(const uint8_t *,size_t);
    int awdlIbssTxComplete(unsigned,unsigned,uint32_t);
    void awdlIbssTxTick();
    void awdlIbssTxCancel();
    void awdlIbssTxStopped();
    void awdlIbssTxDispose();
    IOReturn awdlBatchRequest(uint32_t command,uint32_t run,uint32_t mode);
    void awdlBatchPoll();
    void awdlBatchPublish();

    IOTimerEventSource *awdlIbssTimer = nullptr;
    bool awdlIbssWorkPending = false;
    uint64_t awdlIbssWorkerEpoch = 0;
    ItlAwdl::NativeSearchOwner awdlNativeSearch;
    ItlAwdl::NativeSearchControlTx awdlNativeControlTx;
    ItlAwdl::NativeDiscoverySession awdlNativeDiscovery;
    bool awdlNativeDiscoveryReady();
    bool awdlNativeDiscoveryOpen(OSObject *owner,uint64_t epoch);
    int awdlNativeDiscoveryOffer(OSObject *owner,const uint8_t *,size_t);
    void awdlNativeDiscoveryStop();
    void awdlNativeDiscoveryTick();
    void awdlNativeDiscoveryReceive(const uint8_t *,size_t,unsigned channel,uint32_t gp2);
    void awdlNativeDiscoveryPublish();
    bool awdlNativeControlReadable(IOWorkLoop *loop){
#ifdef AIRPORT_VTD
        return loop&&getMainWorkLoop()==loop&&loop->inGate()&&!dmaDetaching;
#else
        return false;
#endif
    }
    bool awdlNativeSearchReserved();
    bool awdlNativeSearchMayConfigure();
    bool awdlNativeSearchConsumeRx();
    bool awdlNativeSearchAddress(uint8_t out[6]);
    IOReturn awdlNativeSearchRequest(bool stop,uint64_t expectedEpoch=0,bool receiveValidation=false);
    void awdlNativeSearchTick(uint64_t epoch);
    void awdlNativeSearchPublish();
    IOReturn awdlNativeSearchEnable();
    bool awdlNativeSearchEnabled();
    bool awdlNativeSyncRequested = false;
    volatile uint64_t awdlHostSearchStopEpoch = 0;
    volatile SInt32 awdlHostSearchStopPending = 0;
    bool awdlNativeSearchMasterChannel(uint32_t &channel);
    void awdlNativeSyncStatePreflight(const void *payload,unsigned length);
    IOReturn awdlNativeSyncParams(void *payload,unsigned length,OSObject *owner,bool set);
    IOReturn awdlNativeStartMaster(OSObject *owner,uint32_t period,bool warmParams=false);
    bool awdlNativeSyncRoleReady();
    bool awdlNativeSyncRoleConfirmed();
    IOReturn awdlNativeSyncRole(void *payload,unsigned length,OSObject *owner,bool set);
    IOReturn awdlNativeElectionParams(const void *payload,size_t length,OSObject *owner);
    IOReturn awdlNativeElectionMetric(const void *payload,size_t length,OSObject *owner);
    IOReturn awdlNativeElectionMetricRead(uint32_t &metric,uint8_t &overrideMetric);
    void awdlNativeElectionPublish();
    IOReturn awdlNativeUmiRequest(const uint8_t peer[6],const uint8_t *payload,size_t length,
                                  uint8_t kind);
    void awdlNativeUmiPublish();
    IOReturn awdlNativeControlTxArm(uint64_t epoch,bool periodic=false);
    void awdlNativeControlTxTick();
    void awdlNativeControlTxStop(unsigned reason);
    void awdlNativeControlTxPublish();
    IOReturn awdlNativeSearchReadMasterChannel(uint32_t &channel,bool &reserved);
    void *awdlIbssWorkerPrepare();
    bool awdlIbssWorkerSubmit(void *);
    void awdlIbssWorkerDiscard(void *);
    static void awdlIbssExperimentWorker(void *,void *);
    void awdlIbssExperimentPublish();
    void awdlIbssStaRecoveryObserve();
    void awdlIbssLifecycleSource(ItlIbss::LifeCode,uint32_t detail=0);
    void awdlIbssPassiveTraceStart();
    // AWDL resource observation only; native command behavior is unchanged.
    uint32_t awdlP2pResourceSubmit(uint32_t,uint32_t,uint16_t,const void *,bool);
    void awdlP2pResourceReturn(uint32_t,uint32_t,uint32_t,int,bool,uint32_t);
    void awdlIbssFirmwareFault(uint32_t,ItlIbss::FaultRead,const ItlIbss::FaultSnapshot &);
    int tsleep_nsec(void *,int,const char *,int);
    void wakeupOn(void *);
    template<class F> int awdlIbssInitStep(unsigned code,F operation) {
        awdlIbssExperimentEvent(11,code,0,0,0);
        int error=operation();awdlIbssExperimentEvent(12,code,error,0,0);return error;
    }
    void awdlIbssExperimentTick();
    void awdlIbssExperimentEvent(unsigned,unsigned,int,unsigned,unsigned);
    void awdlIbssExperimentWire(const void *,size_t,bool);
    void awdlIbssDataRxStage(unsigned,const ItlAwdl::RawRxMetadata &);
    void awdlIbssExperimentCommon(mbuf_t,int,uint32_t,uint32_t);
    void awdlIbssExperimentBeacon(const void *,size_t);
    IOReturn awdlIbssExperimentTimerPrepare();
    void awdlIbssExperimentTimerStart();
    void awdlIbssExperimentTimerStop();
    static void awdlIbssExperimentTimerAction(OSObject *,IOTimerEventSource *);
    friend struct ItlIbssExclusiveTransport;
    int awdlIbssExclusiveBegin(uint64_t);
    int awdlIbssExclusiveEnd();
    int awdlIbssExclusivePoll();
    int awdlIbssCandidateConfigure();
    // Isolated candidate only; nonvirtual, explicit default-off experiment entry.
    int awdlIbssCandidateCommand(ItlIbss::IwmSession &, ItlIbss::Action,
                                const uint8_t *, size_t);

    ItlAwdl::AuxRocSession awdlRocSession;
    uint32_t awdlRocChannel = 0, awdlRocTicks = 0;
    bool awdlRocAttempted = false, awdlRocCancelSent = false;
    uint64_t awdlRocDeadlineNs = 0;
    uint32_t awdlRocRecordedStarts = 0, awdlRocRecordedEnds = 0, awdlRocRecordedFailures = 0;
    uint32_t awdlMasterChannel = 0; // software dwell policy; main work-loop gate
    bool awdlEngineCycleAccounted = false;
    IOReturn setAwdlMasterChannel(uint32_t channel) override;
    void awdlEngineAccountCycle();
    IOReturn awdlArmValidation();
    void awdlRocTick();
    void awdlRocNotify(const uint8_t *, size_t);
    void awdlRocWindowReady();
    void awdlRocCancel();
    void awdlRocQuiesced();
    void awdlRocPublish();
    bool awdlPeerRocEnabled = false;
    bool awdlPeerRocEngine = false;
    bool awdlRawRxEnabled = false;
    void awdlRawRxRecord(unsigned stage,const ItlAwdl::RawRxMetadata &metadata);
    void awdlRawRxCommon(mbuf_t,int,uint32_t,uint32_t);
    void awdlRawRxWire(unsigned,const void*,size_t,bool);
    void awdlRxIngressEvent(unsigned,uint32_t,uint32_t);
    bool awdlDiscoveryEnabled = false, awdlRocIsDiscovery = false;
    bool awdlRxControlEnabled=false;
    uint32_t awdlRxControlHomeChannel=0;
    ItlAwdl::ReceiveWindow awdlRawRxWindow;
    IOTimerEventSource *awdlDiscoveryTimer = nullptr;
    ItlAwdl::DiscoveryPolicy awdlDiscoveryPolicy;
    uint32_t awdlRocDurationTu=16, awdlRocHomeChannel=0;
    uint32_t awdlRocRxFrames=0,awdlRocAwdlFrames=0,awdlRocBeaconFrames=0,awdlRocOffHomeFrames=0;
    void awdlDiscoveryTimerStart();
    void awdlDiscoveryTimerStop();
    static void awdlDiscoveryTimerAction(OSObject *, IOTimerEventSource *);

    uint32_t awdlPeerRocAttempts = 0, awdlPeerRocDelayTu = 0;
    uint32_t awdlPeerRocRequestedGp2 = 0, awdlPeerRocResponseStatus = UINT32_MAX;
    ItlAwdl::AuxRocCommand awdlPeerRocCommand = {};
    void awdlPeerRocRecord(uint32_t kind, const uint8_t *notification = nullptr, size_t length = 0);
    ItlAwdl::PeerRocPlan awdlPeerRocPlan;
    bool awdlAuxTxEnabled = false;
    bool awdlTemplateTxEnabled = false;
    bool awdlNativeElectionValid = false;
    uint8_t awdlNativeElectionRaw[56] = {};
    uint8_t awdlNativeElectionMaster[6] = {};
    uint32_t awdlNativeElectionRootMetric = 0;
    uint32_t awdlNativeElectionSelfMetric = 0;
    bool awdlNativeElectionMetricValid = false;
    uint32_t awdlNativeElectionMetricRequested = 0;
    uint8_t awdlNativeElectionMetricOverride = 0;
    uint32_t awdlTemplateSequence = 0;
    unsigned awdlScanSubmitting = 0; // protected by main work-loop gate
    ItlAwdl::AuxTxOwnership awdlAuxTx;
    IOReturn awdlArmSingleDataProbe(const ItlAwdl::SingleDataRequest &,const uint8_t local[6]);
    bool awdlSingleDataProbeReady(uint32_t *epoch=nullptr);
    void awdlSingleDataProbePublish();
    void awdlAuxTxStart(bool dataOnly=false);
    void awdlAuxTxTick();
    bool awdlAuxTxDrain();
    bool awdlAuxTxComplete(unsigned queue, unsigned index, uint32_t status);
    void awdlAuxTxPublish();
#endif
    // Diagnostic only. The firmware revisits a channel once per scan visit; asking
    // for more visits is the only dwell control this driver exposes. 0 keeps every
    // scan byte-identical to the stock behaviour, and the boot argument that sets
    // it is honoured only when the AWDL probe is enabled.
    uint32_t awdlDwellIterations = 0;
    uint16_t awdlScanIterations(int channel) const;
    //kext
    void free() override;
    virtual bool attach(IOPCIDevice *device) override;
    virtual void detach(IOPCIDevice *device) override;
#ifdef AIRPORT_VTD
    bool inRateControlGate() {
        return getMainWorkLoop()->inGate();
    }

    template <typename Action>
    IOReturn runRateControlAction(Action action) {
        if (inRateControlGate())
            return action();
        return getMainCommandGate()->runAction(
            [](OSObject *, void *arg0, void *, void *, void *) -> IOReturn {
                return (*static_cast<Action *>(arg0))();
            }, &action);
    }
#endif
    IOReturn enable(IONetworkInterface *netif) override;
    IOReturn disable(IONetworkInterface *netif) override;
    virtual struct ieee80211com *get80211Controller() override;
#ifdef AIRPORT_WCL
    IOReturn setScanHomeAwayTime(uint32_t milliseconds) override {
        com.sc_ic.ic_wcl_scan_away_time_ms = milliseconds;
        return kIOReturnSuccess;
    }
#endif
    
    static bool intrFilter(OSObject *object, IOFilterInterruptEventSource *src);
    static IOReturn _iwm_start_task(OSObject *target, void *arg0, void *arg1, void *arg2, void *arg3);
    
    void releaseAll();
    
    struct _ifnet *getIfp();
    struct iwm_softc *getSoft();
    IOEthernetInterface *getNetworkInterface();
    
    virtual ItlDriverInfo *getDriverInfo() override;
    
    virtual ItlDriverController *getDriverController() override;
    
    //driver info
    virtual const char *getFirmwareVersion() override;
    
    virtual int16_t getBSSNoise() override;
    
    virtual bool is5GBandSupport() override;
    
    virtual int getTxNSS() override;
    
    virtual const char *getFirmwareName() override;
    
    virtual UInt32 supportedFeatures() override;

    virtual const char *getFirmwareCountryCode() override;

    virtual uint32_t getTxQueueSize() override;
    
    //driver controller
    virtual void clearScanningFlags() override;
    
    virtual IOReturn setMulticastList(IOEthernetAddress *addr, int count) override;
    
    //utils
    int    iwm_send_bt_init_conf(struct iwm_softc *);
    
    //fw
    static uint8_t iwm_fw_valid_tx_ant(struct iwm_softc *sc);
    static uint8_t iwm_fw_valid_rx_ant(struct iwm_softc *sc);
    void    iwm_toggle_tx_ant(struct iwm_softc *sc, uint8_t *ant);
    uint32_t iwm_get_tx_ant(struct iwm_softc *sc, struct ieee80211_node *ni,
                            int type, struct ieee80211_frame *wh);
    
    //scan
    uint8_t iwm_umac_scan_fill_channels(struct iwm_softc *sc,
                                        struct iwm_scan_channel_cfg_umac *chan, int n_ssids, int bgscan);
    
    //coex
    uint16_t iwm_coex_agg_time_limit(struct iwm_softc *, struct ieee80211_node *);
    uint8_t iwm_coex_tx_prio(struct iwm_softc *, struct ieee80211_frame *, uint8_t);
    static bool iwm_coex_is_ant_avail(struct iwm_softc *, u8);
    static bool iwm_coex_is_mimo_allowed(struct iwm_softc *, struct ieee80211_node *);
    static bool iwm_coex_is_tpc_allowed(struct iwm_softc *, bool);
    static bool iwm_coex_is_shared_ant_avail(struct iwm_softc *);
    
    uint8_t iwm_lookup_cmd_ver(struct iwm_softc *, uint8_t, uint8_t);
    int    iwm_store_cscheme(struct iwm_softc *, uint8_t *, size_t);
    int    iwm_firmware_store_section(struct iwm_softc *, enum iwm_ucode_type,
                                      uint8_t *, size_t);
    int    iwm_set_default_calib(struct iwm_softc *, const void *);
    void    iwm_fw_info_free(struct iwm_fw_info *);
    int    iwm_read_firmware(struct iwm_softc *, enum iwm_ucode_type);
    uint32_t iwm_read_prph_unlocked(struct iwm_softc *, uint32_t);
    uint32_t iwm_read_prph(struct iwm_softc *, uint32_t);
    void    iwm_write_prph_unlocked(struct iwm_softc *, uint32_t, uint32_t);
    void    iwm_write_prph(struct iwm_softc *, uint32_t, uint32_t);
    void    iwm_write_prph64(struct iwm_softc *, uint64_t, uint64_t);
    int    iwm_read_mem(struct iwm_softc *, uint32_t, void *, int);
    int    iwm_write_mem(struct iwm_softc *, uint32_t, const void *, int);
    int    iwm_write_mem32(struct iwm_softc *, uint32_t, uint32_t);
    int    iwm_poll_bit(struct iwm_softc *, int, uint32_t, uint32_t, int);
    int    iwm_nic_lock(struct iwm_softc *);
    void    iwm_nic_assert_locked(struct iwm_softc *);
    void    iwm_nic_unlock(struct iwm_softc *);
    void    iwm_set_bits_mask_prph(struct iwm_softc *, uint32_t, uint32_t,
                                   uint32_t);
    void    iwm_set_bits_prph(struct iwm_softc *, uint32_t, uint32_t);
    void    iwm_clear_bits_prph(struct iwm_softc *, uint32_t, uint32_t);
    int    iwm_dma_contig_alloc(bus_dma_tag_t, struct iwm_dma_info *, bus_size_t, bus_size_t);
    void    iwm_dma_contig_free(struct iwm_dma_info *);
    int    iwm_alloc_rx_ring(struct iwm_softc *, struct iwm_rx_ring *);
    void    iwm_disable_rx_dma(struct iwm_softc *);
    void    iwm_reset_rx_ring(struct iwm_softc *, struct iwm_rx_ring *);
    void    iwm_free_rx_ring(struct iwm_softc *, struct iwm_rx_ring *);
    int    iwm_alloc_tx_ring(struct iwm_softc *, struct iwm_tx_ring *, int);
    void    iwm_reset_tx_ring(struct iwm_softc *, struct iwm_tx_ring *);
    void    iwm_free_tx_ring(struct iwm_softc *, struct iwm_tx_ring *);
    void    iwm_enable_rfkill_int(struct iwm_softc *);
    int    iwm_check_rfkill(struct iwm_softc *);
    void    iwm_enable_interrupts(struct iwm_softc *);
    void    iwm_enable_fwload_interrupt(struct iwm_softc *);
    void    iwm_restore_interrupts(struct iwm_softc *);
    void    iwm_disable_interrupts(struct iwm_softc *);
    void    iwm_ict_reset(struct iwm_softc *);
    int    iwm_set_hw_ready(struct iwm_softc *);
    int    iwm_prepare_card_hw(struct iwm_softc *);
    void    iwm_apm_config(struct iwm_softc *);
    int    iwm_apm_init(struct iwm_softc *);
    void    iwm_apm_stop(struct iwm_softc *);
    int    iwm_allow_mcast(struct iwm_softc *);
    void    iwm_init_msix_hw(struct iwm_softc *);
    void    iwm_conf_msix_hw(struct iwm_softc *, int);
    int    iwm_clear_persistence_bit(struct iwm_softc *);
    int    iwm_start_hw(struct iwm_softc *);
    void    iwm_stop_device(struct iwm_softc *);
    void    iwm_nic_config(struct iwm_softc *);
    int    iwm_nic_rx_init(struct iwm_softc *);
    int    iwm_nic_rx_legacy_init(struct iwm_softc *);
    int    iwm_nic_rx_mq_init(struct iwm_softc *);
    int    iwm_nic_tx_init(struct iwm_softc *);
    int    iwm_nic_init(struct iwm_softc *);
    int    iwm_enable_ac_txq(struct iwm_softc *, int, int);
    int    iwm_enable_txq(struct iwm_softc *, int, int, int, int, int, int);
    int    iwm_disable_txq(struct iwm_softc *, uint8_t, uint8_t, uint8_t);
    int    iwm_enable_default_tx_queues(struct iwm_softc *);
    int    iwm_disable_tx_queues(struct iwm_softc *);
    int    iwm_post_alive(struct iwm_softc *);
    struct iwm_phy_db_entry *iwm_phy_db_get_section(struct iwm_softc *, uint16_t,
                                                    uint16_t);
    int    iwm_phy_db_set_section(struct iwm_softc *,
                                  struct iwm_calib_res_notif_phy_db *);
    int    iwm_is_valid_channel(uint16_t);
    uint8_t    iwm_ch_id_to_ch_index(uint16_t);
    uint16_t iwm_channel_id_to_papd(uint16_t);
    uint16_t iwm_channel_id_to_txp(struct iwm_softc *, uint16_t);
    int    iwm_phy_db_get_section_data(struct iwm_softc *, uint32_t, uint8_t **,
                                       uint16_t *, uint16_t);
    int    iwm_send_phy_db_cmd(struct iwm_softc *, uint16_t, uint16_t, void *);
    int    iwm_phy_db_send_all_channel_groups(struct iwm_softc *, uint16_t,
                                              uint8_t);
    int    iwm_send_phy_db_data(struct iwm_softc *);
    void    iwm_te_v2_to_v1(const struct iwm_time_event_cmd_v2 *,
                            struct iwm_time_event_cmd_v1 *);
    int    iwm_send_time_event_cmd(struct iwm_softc *,
                                   const struct iwm_time_event_cmd *);
    void    iwm_protect_session(struct iwm_softc *, struct iwm_node *, uint32_t,
                                uint32_t);
    void    iwm_unprotect_session(struct iwm_softc *, struct iwm_node *);
    int    iwm_nvm_read_chunk(struct iwm_softc *, uint16_t, uint16_t, uint16_t,
                              uint8_t *, uint16_t *);
    int    iwm_nvm_read_section(struct iwm_softc *, uint16_t, uint8_t *,
                                uint16_t *, size_t);
    void    iwm_init_channel_map(struct iwm_softc *, const uint16_t * const,
                                 const uint8_t *nvm_channels, size_t nchan);
    int    iwm_mimo_enabled(struct iwm_softc *);
    void    iwm_setup_ht_rates(struct iwm_softc *);
    void    iwm_setup_vht_rates(struct iwm_softc *);
    static void    iwm_mac_ctxt_task(void *);
    static void    iwm_chan_ctxt_task(void *);
    static void    iwm_updateprot(struct ieee80211com *);
    static void    iwm_updateslot(struct ieee80211com *);
    static void    iwm_updateedca(struct ieee80211com *);
    static void    iwm_updatedtim(struct ieee80211com *);
    void   iwm_init_reorder_buffer(struct iwm_reorder_buffer *, uint16_t,
                                   uint16_t);
    void   iwm_clear_reorder_buffer(struct iwm_softc *, struct iwm_rxba_data *);
    static int    iwm_ampdu_rx_start(struct ieee80211com *, struct ieee80211_node *,
                                     uint8_t);
    static void    iwm_ampdu_rx_stop(struct ieee80211com *, struct ieee80211_node *,
                                     uint8_t);
    static void   iwm_rx_ba_session_expired(void *);
    static void   iwm_reorder_timer_expired(void *);
    static uint8_t iwm_num_of_ant(uint8_t mask);
    int    iwm_sta_rx_agg(struct iwm_softc *, struct ieee80211_node *, uint8_t,
                           uint16_t, uint16_t, int, int);
    static int    iwm_ampdu_tx_start(struct ieee80211com *, struct ieee80211_node *,
                              uint8_t);
    static void    iwm_ampdu_tx_stop(struct ieee80211com *, struct ieee80211_node *,
                              uint8_t);
    static void     iwm_update_chw(struct ieee80211com *);
    int iwm_sta_tx_agg(struct iwm_softc *, struct ieee80211_node *,
                        uint8_t, uint8_t, uint16_t, int);
    static void    iwm_ba_task(void *);
    
    int    iwm_parse_nvm_data(struct iwm_softc *, const uint16_t *,
                              const uint16_t *, const uint16_t *,
                              const uint16_t *, const uint16_t *,
                              const uint16_t *, int);
    void    iwm_set_hw_address_8000(struct iwm_softc *, struct iwm_nvm_data *,
                                    const uint16_t *, const uint16_t *);
    int    iwm_parse_nvm_sections(struct iwm_softc *, struct iwm_nvm_section *);
    int    iwm_nvm_init(struct iwm_softc *);
    int    iwm_firmware_load_sect(struct iwm_softc *, uint32_t, const uint8_t *,
                                  uint32_t);
    int    iwm_firmware_load_chunk(struct iwm_softc *, uint32_t, const uint8_t *,
                                   uint32_t);
    int    iwm_load_firmware_7000(struct iwm_softc *, enum iwm_ucode_type);
    int    iwm_load_cpu_sections_8000(struct iwm_softc *, struct iwm_fw_sects *,
                                      int , int *);
    int    iwm_load_firmware_8000(struct iwm_softc *, enum iwm_ucode_type);
    int    iwm_load_firmware(struct iwm_softc *, enum iwm_ucode_type);
    int    iwm_start_fw(struct iwm_softc *, enum iwm_ucode_type);
    int    iwm_send_tx_ant_cfg(struct iwm_softc *, uint8_t);
    int    iwm_send_phy_cfg_cmd(struct iwm_softc *);
    int    iwm_load_ucode_wait_alive(struct iwm_softc *, enum iwm_ucode_type);
    int    iwm_send_dqa_cmd(struct iwm_softc *);
    int    iwm_run_init_mvm_ucode(struct iwm_softc *, int);
    int    iwm_config_ltr(struct iwm_softc *);
    int    iwm_rx_addbuf(struct iwm_softc *, int, int);
    int    iwm_get_signal_strength(struct iwm_softc *, struct ieee80211_rx_status *, struct iwm_rx_phy_info *);
    int    iwm_rxmq_get_signal_strength(struct iwm_softc *, struct ieee80211_rx_status *, uint32_t, struct iwm_rx_mpdu_desc *);
    void    iwm_rx_rx_phy_cmd(struct iwm_softc *, struct iwm_rx_packet *,
                              struct iwm_rx_data *);
    int    iwm_get_noise(const struct iwm_statistics_rx_non_phy *);
    int    iwm_rx_hwdecrypt(struct iwm_softc *, mbuf_t, uint32_t,
               struct ieee80211_rxinfo *);
    int    iwm_ccmp_decap(struct iwm_softc *, mbuf_t,
                          struct ieee80211_node *, struct ieee80211_rxinfo *);
    void    iwm_rx_frame(struct iwm_softc *, mbuf_t, int, uint32_t, int, int,
                         uint32_t, struct ieee80211_rxinfo *, struct mbuf_list *);
    void    iwm_rx_tx_cmd_single(struct iwm_softc *, struct iwm_tx_resp *,
                                 int, int);
    void    iwm_ampdu_tx_done(struct iwm_softc *, struct iwm_cmd_header *,
                              struct iwm_node *, struct iwm_tx_ring *, uint32_t, uint8_t,
                              uint8_t, uint16_t, int, struct iwm_agg_tx_status *);
    void    iwm_rx_tx_ba_notif(struct iwm_softc *, struct iwm_rx_packet *, struct iwm_rx_data *);
    void    iwm_rx_tx_cmd(struct iwm_softc *, struct iwm_rx_packet *,
                          struct iwm_rx_data *);
    void    iwm_ampdu_rate_control(struct iwm_softc *, struct ieee80211_node *, struct iwm_tx_ring *, uint16_t, uint16_t, struct ieee80211_tx_info *, int, uint32_t);
    void iwm_rx_mpdu_mq(struct iwm_softc *sc, mbuf_t m, void *pktdata,
                   size_t maxlen, struct mbuf_list *ml, size_t diagnosticWireLength);
    void    iwm_rx_bmiss(struct iwm_softc *, struct iwm_rx_packet *,
                         struct iwm_rx_data *);
    int    iwm_binding_cmd(struct iwm_softc *, struct iwm_node *, uint32_t);
    int    iwm_phy_ctxt_cmd_uhb(struct iwm_softc *, struct iwm_phy_ctxt *, uint8_t,
                                uint8_t, uint32_t, uint32_t);
    void    iwm_phy_ctxt_cmd_hdr(struct iwm_softc *, struct iwm_phy_ctxt *,
                                 struct iwm_phy_context_cmd *, uint32_t, uint32_t);
    void    iwm_phy_ctxt_cmd_data(struct iwm_softc *, struct iwm_phy_context_cmd *,
                                  struct ieee80211_channel *, uint8_t, uint8_t);
    int    iwm_phy_ctxt_cmd(struct iwm_softc *, struct iwm_phy_ctxt *, uint8_t,
                            uint8_t, uint32_t, uint32_t);
    int    iwm_send_cmd(struct iwm_softc *, struct iwm_host_cmd *);
    int    iwm_send_cmd_pdu(struct iwm_softc *, uint32_t, uint32_t, uint16_t,
                            const void *);
    int    iwm_send_cmd_status(struct iwm_softc *, struct iwm_host_cmd *,
                               uint32_t *);
    int    iwm_send_cmd_pdu_status(struct iwm_softc *, uint32_t, uint16_t,
                                   const void *, uint32_t *);
    void    iwm_free_resp(struct iwm_softc *, struct iwm_host_cmd *);
    void    iwm_cmd_done(struct iwm_softc *, int, int, int);
    void    iwm_update_sched(struct iwm_softc *, int, int, uint8_t, uint16_t);
    void    iwm_reset_sched(struct iwm_softc *, int, int, uint8_t);
    const struct iwl_rs_rate_info *iwm_tx_fill_cmd(struct iwm_softc *, struct iwm_node *,
                                           struct ieee80211_frame *, struct iwm_tx_cmd *);
    void iwm_txd_done(struct iwm_softc *, struct iwm_tx_data *);
    void iwm_ampdu_txq_advance(struct iwm_softc *, struct iwm_tx_ring *, int);
    void iwm_clear_oactive(struct iwm_softc *, struct iwm_tx_ring *);
    int    iwm_tx(struct iwm_softc *, mbuf_t, struct ieee80211_node *, int);
    int    iwm_flush_tx_path(struct iwm_softc *, int);
    void    iwm_led_enable(struct iwm_softc *);
    void    iwm_led_disable(struct iwm_softc *);
    int    iwm_led_is_enabled(struct iwm_softc *);
    static void    iwm_led_blink_timeout(void *);
    void    iwm_led_blink_start(struct iwm_softc *);
    void    iwm_led_blink_stop(struct iwm_softc *);
    int    iwm_beacon_filter_send_cmd(struct iwm_softc *,
                                      struct iwm_beacon_filter_cmd *);
    void    iwm_beacon_filter_set_cqm_params(struct iwm_softc *, struct iwm_node *,
                                             struct iwm_beacon_filter_cmd *);
    int    iwm_update_beacon_abort(struct iwm_softc *, struct iwm_node *, int);
    void    iwm_power_build_cmd(struct iwm_softc *, struct iwm_node *,
                                struct iwm_mac_power_cmd *);
    int    iwm_power_mac_update_mode(struct iwm_softc *, struct iwm_node *);
    int    iwm_power_update_device(struct iwm_softc *);
    int    iwm_enable_beacon_filter(struct iwm_softc *, struct iwm_node *);
    int    iwm_disable_beacon_filter(struct iwm_softc *);
    int    iwm_add_sta_cmd(struct iwm_softc *, struct iwm_node *, int, unsigned int);
    int    iwm_add_aux_sta(struct iwm_softc *);
    int    iwm_rm_sta_cmd(struct iwm_softc *, struct iwm_node *);
    int    iwm_drain_sta(struct iwm_softc *, struct iwm_node *, bool);
    uint16_t iwm_scan_rx_chain(struct iwm_softc *);
    uint32_t iwm_scan_rate_n_flags(struct iwm_softc *, int, int);
    uint8_t    iwm_lmac_scan_fill_channels(struct iwm_softc *,
                                           struct iwm_scan_channel_cfg_lmac *, int, int);
    int iwm_fill_probe_req_v1(struct iwm_softc *, struct iwm_scan_probe_req_v1 *);
    int    iwm_fill_probe_req(struct iwm_softc *, struct iwm_scan_probe_req *);
    int    iwm_lmac_scan(struct iwm_softc *, int);
    int    iwm_config_umac_scan(struct iwm_softc *);
    int    iwm_umac_scan_size(struct iwm_softc *sc);
    struct iwm_scan_umac_chan_param *iwm_get_scan_req_umac_chan_param(struct iwm_softc *sc, struct iwm_scan_req_umac *req);
    void *iwm_get_scan_req_umac_data(struct iwm_softc *sc, struct iwm_scan_req_umac *req);
    int    iwm_umac_scan(struct iwm_softc *, int);
    void    iwm_mcc_update(struct iwm_softc *, struct iwm_mcc_chub_notif *);
    uint8_t    iwm_ridx2rate(struct ieee80211_rateset *, int);
    void    iwm_ack_rates(struct iwm_softc *, struct iwm_node *, int *, int *);
    void    iwm_mac_ctxt_cmd_common(struct iwm_softc *, struct iwm_node *,
                                    struct iwm_mac_ctx_cmd *, uint32_t);
    void    iwm_mac_ctxt_cmd_fill_sta(struct iwm_softc *, struct iwm_node *,
                                      struct iwm_mac_data_sta *, int);
    int    iwm_mac_ctxt_cmd(struct iwm_softc *, struct iwm_node *, uint32_t, int);
    int    iwm_update_quotas(struct iwm_softc *, struct iwm_node *, int);
    void    iwm_add_task(struct iwm_softc *, struct taskq *, struct task *);
    void    iwm_del_task(struct iwm_softc *, struct taskq *, struct task *);
    int    iwm_scan(struct iwm_softc *);
    static int    iwm_bgscan(struct ieee80211com *);
    int    iwm_umac_scan_abort(struct iwm_softc *);
    int    iwm_lmac_scan_abort(struct iwm_softc *);
    int    iwm_scan_abort(struct iwm_softc *);
    int    iwm_phy_ctxt_update(struct iwm_softc *, struct iwm_phy_ctxt *,
                               struct ieee80211_channel *, uint8_t, uint8_t, uint32_t);
    int    iwm_auth(struct iwm_softc *);
    int    iwm_deauth(struct iwm_softc *);
    int    iwm_run(struct iwm_softc *);
    int    iwm_run_stop(struct iwm_softc *);
    static struct ieee80211_node *iwm_node_alloc(struct ieee80211com *);
    int    iwm_set_key_v1(struct ieee80211com *, struct ieee80211_node *,
                          struct ieee80211_key *);
    static int    iwm_set_key(struct ieee80211com *, struct ieee80211_node *,
                              struct ieee80211_key *);
    void    iwm_delete_key_v1(struct ieee80211com *,
                              struct ieee80211_node *, struct ieee80211_key *);
    static void    iwm_delete_key(struct ieee80211com *,
                                  struct ieee80211_node *, struct ieee80211_key *);
    static void    iwm_calib_timeout(void *);
    int    iwm_media_change(struct _ifnet *);
    static void    iwm_newstate_task(void *);
    static int    iwm_newstate(struct ieee80211com *, enum ieee80211_state, int);
    void    iwm_endscan(struct iwm_softc *);
    void    iwm_fill_sf_command(struct iwm_softc *, struct iwm_sf_cfg_cmd *,
                                struct ieee80211_node *);
    int    iwm_sf_config(struct iwm_softc *, int);
    int    iwm_send_update_mcc_cmd(struct iwm_softc *, const char *);
    int    iwm_send_soc_conf(struct iwm_softc *);
    int    iwm_send_temp_report_ths_cmd(struct iwm_softc *);
    void    iwm_tt_tx_backoff(struct iwm_softc *, uint32_t);
    int iwm_fill_paging_mem(struct iwm_softc *, const struct iwm_fw_sects *);
    int iwm_alloc_fw_paging_mem(struct iwm_softc *, const struct iwm_fw_sects *);
    void    iwm_free_fw_paging(struct iwm_softc *);
    int    iwm_save_fw_paging(struct iwm_softc *, const struct iwm_fw_sects *);
    int    iwm_send_paging_cmd(struct iwm_softc *, const struct iwm_fw_sects *);
    int    iwm_init_hw(struct iwm_softc *);
    int    iwm_init(struct _ifnet *);
    static void    iwm_start(struct _ifnet *);
    void    iwm_stop(struct _ifnet *);
    static void    iwm_watchdog(struct _ifnet *);
    static int    iwm_ioctl(struct _ifnet *, u_long, caddr_t);
#ifdef IWM_DEBUG
    const char *iwm_desc_lookup(uint32_t);
    void    iwm_nic_error(struct iwm_softc *);
    void    iwm_nic_umac_error(struct iwm_softc *);
#endif
    void    iwm_rx_mpdu(struct iwm_softc *, mbuf_t, void *, size_t,
                        struct mbuf_list *, size_t diagnosticWireLength);
    void   iwm_flip_address(uint8_t *);
    int    iwm_detect_duplicate(struct iwm_softc *, mbuf_t,
               struct iwm_rx_mpdu_desc *, struct ieee80211_rxinfo *);
    int    iwm_is_sn_less(uint16_t, uint16_t, uint16_t);
    void   iwm_release_frames(struct iwm_softc *, struct ieee80211_node *,
               struct iwm_rxba_data *, struct iwm_reorder_buffer *, uint16_t,
               struct mbuf_list *);
    int    iwm_oldsn_workaround(struct iwm_softc *, struct ieee80211_node *,
               int, struct iwm_reorder_buffer *, uint32_t, uint32_t);
    int    iwm_rx_reorder(struct iwm_softc *, mbuf_t, int,
               struct iwm_rx_mpdu_desc *, int, int, uint32_t,
               struct ieee80211_rxinfo *, struct mbuf_list *);
    int    iwm_rx_pkt_valid(struct iwm_rx_packet *);
    void    iwm_rx_pkt(struct iwm_softc *, struct iwm_rx_data *,
                       struct mbuf_list *);
    void    iwm_notif_intr(struct iwm_softc *);
    static int    iwm_intr(OSObject *object, IOInterruptEventSource* sender, int count);
    static int    iwm_intr_msix(OSObject *object, IOInterruptEventSource* sender, int count);
    static int    iwm_match(IOPCIDevice *);
    int    iwm_preinit(struct iwm_softc *);
    void    iwm_attach_hook(struct device *);
    bool    iwm_attach(struct iwm_softc *, struct pci_attach_args *);
    static void    iwm_init_task(void *);
    int    iwm_activate(struct iwm_softc *, int);
    int    iwm_resume(struct iwm_softc *);
    
    
    
public:
    IOInterruptEventSource* fInterrupt;
    IOPCIDevice *pciNub;
    struct pci_attach_args pci;
    struct iwm_softc com;
};

#endif /* ItlIwm_hpp */
