// SPDX-License-Identifier: GPL-2.0-or-later
// Dedicated one-frame AUX transport probe. Ordinary AP submission is unchanged.
#pragma once
#include "SyncTemplate.hpp"
#include "TemplateActionBuilder.hpp"
#include "AuxTxCommand.hpp"
#include "SingleDataProbe.hpp"
static_assert(sizeof(iwm_tx_cmd) == 56 && sizeof(iwm_cmd_header) == 4, "legacy AUX TX wire ABI");
void ItlIwm::awdlSingleDataProbePublish(){
    auto controller=getController();if(!controller)return;
    auto &p=ItlAwdl::gSingleDataProbe;
    p.publishedBlockReason=p.lastBlockReason;
    controller->setProperty("AWDLSingleDataProbeSchema",1,32);
    controller->setProperty("AWDLSingleDataProbePhase",uint32_t(p.phase),32);
    controller->setProperty("AWDLSingleDataProbeToken",p.request.token,64);
    controller->setProperty("AWDLSingleDataProbeGeneration",p.generation,32);
    controller->setProperty("AWDLSingleDataProbeModelEpoch",p.modelEpoch,32);
    controller->setProperty("AWDLSingleDataProbeSubmitted",p.submitted,32);
    controller->setProperty("AWDLSingleDataProbeCompleted",p.completed,32);
    controller->setProperty("AWDLSingleDataProbeStatus",p.status,32);
    controller->setProperty("AWDLSingleDataProbeReason",p.reason,32);
    controller->setProperty("AWDLSingleDataProbeLastBlock",p.lastBlockReason,32);
    controller->setProperty("AWDLSingleDataProbeAckReported",p.ackReported);
    controller->setProperty("AWDLSingleDataProbeReplyObserved",p.replyObserved);
    controller->setProperty("AWDLSingleDataProbePending",p.pending);
    controller->setProperty("AWDLSingleDataProbeCancelled",p.cancelled);
    controller->setProperty("AWDLSingleDataProbeSentGp2",p.sentGp2,32);
    controller->setProperty("AWDLSingleDataProbeReplyGp2",p.replyGp2,32);
    controller->setProperty("AWDLSingleDataProbeDeadlineNs",p.deadlineNs,64);
    controller->setProperty("AWDLSingleDataProbePeer",const_cast<uint8_t *>(p.request.peer),sizeof(p.request.peer));
    if(p.submitted)controller->setProperty("AWDLSingleDataProbeFrame",const_cast<uint8_t *>(p.frame),sizeof(p.frame));
}
bool ItlIwm::awdlSingleDataProbeReady(uint32_t *epoch){
    auto &p=ItlAwdl::gSingleDataProbe;
    if(!awdlTemplateTxEnabled||(p.phase!=ItlAwdl::SingleDataPhase::Armed&&p.phase!=ItlAwdl::SingleDataPhase::Preparing))return false;
    ItlAwdl::DataContext context;
    if(!ItlAwdl::gEngine.modeEnabled||!ItlAwdl::gEngine.running||!awdlDataContext(context))return p.blocked(10);
    ItlAwdl::TemplateSyncConfig sync;ItlAwdl::TemplateService service;uint32_t revision;
    if(!ItlAwdl::templateSnapshot(sync,service,revision)||!ItlAwdl::templateIdentityValid(service,context.nowNs)||
       !ItlAwdl::gTemplateRadio.rocIdentityMatches(com.sc_generation,service.source))return p.blocked(11);
    if(memcmp(service.source,p.local,6)){p.cancel();return p.blocked(4);}
    if(!p.eligible(context))return false;
    if(epoch)*epoch=context.modelEpoch;return true;
}
void ItlIwm::awdlAuxTxPublish() {
    if (awdlProbeCounters)
        awdlProbeCounters->setRoc(ItlAwdlProbeCounters::TxPhase, uint32_t(awdlAuxTx.phase));
}
bool ItlIwm::awdlAuxTxDrain() { return !awdlAuxTx.pending; }
void ItlIwm::awdlAuxTxStart(bool dataOnly) {
    using C = ItlAwdlProbeCounters;
    auto &umi = gItlNativeUmi;
    const bool umiReady = umi.owner == this && umi.pending && !umi.flight;
    if ((!awdlAuxTxEnabled && !umiReady) || !awdlProbeCounters) return;
    uint8_t raw[ItlAwdl::kSyncTemplateCapacity];
    uint8_t frame[1536];
    ItlAwdl::TemplateFrameInput input;
    uint32_t rawLength = 0,rawRevision=0;
    size_t frameLength = 0;bool singleData=false;
    if (awdlTemplateTxEnabled && !umiReady) {
        if (!ItlAwdl::snapshotSyncTemplate(raw, sizeof(raw), rawLength,&rawRevision)) {
            ItlAwdl::gTemplateRadio.lastSkip=13;
            awdlProbeCounters->add(C::AuxTxSkipped);
            awdlProbeCounters->setRoc(C::TxStatus, 0x80000000u | 13u);
            return;
        }
    }
    auto skip = [this,&singleData](bool resource, uint32_t reason) {
        awdlProbeCounters->add(resource ? C::AuxTxResourceErrors : C::AuxTxSkipped);
        awdlProbeCounters->setRoc(C::TxStatus, 0x80000000u | reason);
        if(awdlTemplateTxEnabled)ItlAwdl::gTemplateRadio.lastSkip=reason;
        if(singleData){ItlAwdl::gSingleDataProbe.failed(reason);awdlSingleDataProbePublish();}
        awdlAuxTx.skipped(); awdlAuxTxPublish();
    };
#ifndef AIRPORT_VTD
    skip(false, 1); return; // this adapter relies on persistent, preallocated bounce DMA
#else
    auto sc = &com; auto ic = &sc->sc_ic;
    if (!getMainWorkLoop()->inGate() || dmaDetaching || awdlScanSubmitting || ic->ic_state != IEEE80211_S_RUN ||
        sc->ns_nstate != IEEE80211_S_RUN ||
        (sc->sc_flags & (IWM_FLAG_SHUTDOWN | IWM_FLAG_HW_ERR | IWM_FLAG_RFKILL |
                         IWM_FLAG_SCANNING | IWM_FLAG_BGSCAN))) { skip(false, 2); return; }
    // Legacy AUX queue 15 shares the ordinary aggregate completion branch.
    // Restrict this first transport probe to the independently routed DQA AUX queue.
    if (!isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_DQA_SUPPORT)) { skip(false, 3); return; }
    const unsigned qid = IWM_DQA_AUX_QUEUE;
    auto ring = &sc->txq[qid]; const unsigned idx = ring->cur;
    if (!ring->desc || !ring->cmd || !sc->sched_dma.vaddr || ring->queued || ring->tail != ring->cur ||
        ring->data[idx].m || !ring->data[idx].map) { skip(true, 4); return; }
    const uint8_t validAnts = iwm_fw_valid_tx_ant(sc);
    const uint8_t antenna = validAnts & uint8_t(-validAnts);
    if (!antenna || !iwm_nic_lock(sc)) { skip(true, 5); return; }
    uint32_t gp2 = iwm_read_prph(sc, IWM_DEVICE_SYSTEM_TIME_REG); iwm_nic_unlock(sc);
    if (awdlTemplateTxEnabled && !umiReady) {
        auto &radio=ItlAwdl::gTemplateRadio;
        if(!radio.startAligned||!radio.targetValid||
           !ItlAwdl::templateSnapshot(input.sync,input.service,input.revision)) {skip(false,16);return;}
        input.clock=radio.clock;input.generation=sc->sc_generation;input.gp2=gp2;
        input.hostNs=itlAwdlRocNowNs();input.leaseEndNs=ItlAwdl::gEngine.deadlineNs;input.startGp2=awdlRocSession.startGp2;input.targetGp2=radio.targetGp2;
        if(!ItlAwdl::templateFrameInputValid(input)){skip(false,17);return;}
        if(!radio.rocIdentityMatches(sc->sc_generation,input.service.source)){skip(false,21);return;}
        const uint16_t sequence = uint16_t(awdlTemplateSequence & 0x0fff);
        uint32_t probeEpoch=0;singleData=awdlSingleDataProbeReady(&probeEpoch);
        if(dataOnly&&!singleData)return;
        if(singleData&&!ItlAwdl::gSingleDataProbe.begin(sc->sc_generation,probeEpoch,itlAwdlRocNowNs()))return;
        const bool built = singleData
            ? ItlAwdl::buildSingleDataEcho(ItlAwdl::gSingleDataProbe.request,input.service.source,sc->sc_generation,
                                          sequence,frame,sizeof(frame),frameLength)
            : ((sequence & 1)&&ItlAwdl::templateServiceValid(input.service,input.hostNs))
            ? ItlAwdl::buildServiceMifAction(raw, rawLength, input, sequence,
                                             frame, sizeof(frame), frameLength)
            : ItlAwdl::buildTemplateAction(raw, rawLength, input, sequence,
                                           frame, sizeof(frame), frameLength);
        if (!built) {
            skip(false, 6); return;
        }
    } else if (umiReady) {
        if (dataOnly) return;
        uint8_t source[6] = {};
        if (!awdlIbssLocalAddress(source) ||
            !ItlAwdl::buildNativeUmiFrame(umi.peer, source, umi.payload, umi.length,
                                          gp2, uint16_t(awdlTemplateSequence & 0x0fff),
                                          frame, sizeof(frame), frameLength)) {
            ++umi.dropped;
            umi.pending = false;
            umi.length = 0;
            awdlNativeUmiPublish();
            skip(false, 25);
            return;
        }
    } else {
        if(dataOnly)return;
        if (!ItlAwdl::buildAuxTxProbe(ic->ic_myaddr, gp2, frame)) { skip(false, 7); return; }
        frameLength = 40;
    }
    auto windowOpen=[&](uint32_t now)->bool {
        if(umiReady)return awdlRocSession.mayTransmit(sc->sc_generation) &&
            awdlRocSession.generation==uint32_t(sc->sc_generation) &&
            int32_t(now-awdlRocSession.startGp2)>=0 &&
            uint32_t(now-awdlRocSession.startGp2)+2000<=awdlRocDurationTu*1024u;
        if(!awdlTemplateTxEnabled)return ItlAwdl::auxTxWindowOpen(sc->sc_generation,
            awdlRocSession.generation,now,awdlRocSession.startGp2,awdlRocSession.mayTransmit(sc->sc_generation));
        return awdlRocSession.mayTransmit(sc->sc_generation)&&
            input.generation==uint32_t(sc->sc_generation)&&int32_t(now-input.startGp2)>=0&&
            uint32_t(now-input.targetGp2)+2000<=uint32_t(input.sync.period)*input.sync.presence*1024u;
    };
    if(!windowOpen(gp2)){skip(false,8);return;}
    mbuf_t m = nullptr; unsigned int chunks = 1;
    if (mbuf_allocpacket(MBUF_DONTWAIT, frameLength, &chunks, &m) || !m) { skip(true, 9); return; }
    if (mbuf_next(m) || mbuf_maxlen(m) < frameLength) { mbuf_freem(m); skip(true, 10); return; }
    mbuf_setlen(m, frameLength); mbuf_pkthdr_setlen(m, frameLength);
    memcpy(mbuf_data(m), frame, frameLength);
    mbuf_adj(m, 24); // MAC header is in the firmware command, action body in bounce DMA
    auto data = &ring->data[idx]; IOPhysicalSegment segments[IWM_NUM_OF_TBS - 2];
    const unsigned nsegs = bus_dmamap_packet(data->map, m, segments, IWM_NUM_OF_TBS - 2, false);
    if (!nsegs) { mbuf_freem(m); skip(true, 11); return; }
    if (!iwm_nic_lock(sc)) { mbuf_freem(m); skip(true, 12); return; }
    gp2 = iwm_read_prph(sc, IWM_DEVICE_SYSTEM_TIME_REG); iwm_nic_unlock(sc);
    if(!windowOpen(gp2)||(awdlTemplateTxEnabled&&!umiReady&&uint32_t(gp2-input.gp2)>2000)){
        mbuf_freem(m);skip(false,14);return;
    }
    auto command = &ring->cmd[idx];
    const unsigned priority=iwm_coex_tx_prio(sc,reinterpret_cast<ieee80211_frame *>(frame),EDCA_AC_BE);
    if(!ItlAwdl::configureAuxTxCommand(frame,frameLength,antenna,priority,data->scratch_paddr,idx,*command)){
        mbuf_freem(m);skip(false,20);return;
    }
    auto tx=reinterpret_cast<iwm_tx_cmd *>(command->data);
    const unsigned tb1 = _ALIGN(sizeof(*tx) + sizeof(iwm_cmd_header) + 24 - 20, 4);
    auto desc = &ring->desc[idx]; memset(desc, 0, sizeof(*desc)); desc->num_tbs = 2 + nsegs;
    desc->tbs[0].lo = htole32(data->cmd_paddr);
    desc->tbs[0].hi_n_len = htole16(iwm_get_dma_hi_addr(data->cmd_paddr) | (20 << 4));
    desc->tbs[1].lo = htole32(data->cmd_paddr + 20);
    desc->tbs[1].hi_n_len = htole16(iwm_get_dma_hi_addr(data->cmd_paddr + 20) | (tb1 << 4));
    for (unsigned i = 0; i < nsegs; ++i) {
        desc->tbs[i+2].lo = htole32(segments[i].location);
        desc->tbs[i+2].hi_n_len = htole16(iwm_get_dma_hi_addr(segments[i].location) | (segments[i].length << 4));
    }
    if(awdlTemplateTxEnabled && !umiReady){
        ItlAwdl::TemplateSyncConfig freshSync;ItlAwdl::TemplateService freshService;uint32_t revision;
        if(!ItlAwdl::syncTemplateRevisionMatches(rawRevision)||
           !ItlAwdl::templateSnapshot(freshSync,freshService,revision)||revision!=input.revision||
           !ItlAwdl::templateIdentityValid(freshService,itlAwdlRocNowNs())||
           !ItlAwdl::gTemplateRadio.rocIdentityMatches(sc->sc_generation,freshService.source)||
           memcmp(freshService.source,input.service.source,6)||
           (!singleData&&frame[30]==3&&(!ItlAwdl::templateServiceValid(freshService,itlAwdlRocNowNs())||
            strcmp(freshService.instance,input.service.instance)||strcmp(freshService.host,input.service.host)||
            freshService.port!=input.service.port||freshService.txtLength!=input.service.txtLength||
            memcmp(freshService.txt,input.service.txt,input.service.txtLength)))){
            mbuf_freem(m);skip(false,18);return;
        }
    }
    if(singleData&&!awdlSingleDataProbeReady()){mbuf_freem(m);skip(false,22);return;}
    if (!awdlAuxTx.attempt()) { mbuf_freem(m);if(singleData){ItlAwdl::gSingleDataProbe.failed(23);awdlSingleDataProbePublish();}return; }
    awdlProbeCounters->add(C::AuxTxAttempts);
    awdlProbeCounters->setRoc(C::TxStatus, UINT32_MAX);
    if (!awdlAuxTx.submit(sc->sc_generation, qid, idx, itlAwdlRocNowNs() + 2000000000ULL)) {
        mbuf_freem(m); skip(true, 15); return;
    }
    if (umiReady) {
        umi.flight = true;
        ++umi.submitted;
        awdlNativeUmiPublish();
    }
    if(singleData&&!ItlAwdl::gSingleDataProbe.publish(sc->sc_generation,qid,idx,gp2,itlAwdlRocNowNs(),frame,frameLength)){
        awdlAuxTx.complete(sc->sc_generation,qid,idx);awdlAuxTx.recycle();mbuf_freem(m);skip(false,24);return;
    }
    data->m = m; data->in = nullptr; data->totlen = int(frameLength);
    data->txmcs = data->txrate = 0;
    data->fc = htole16(uint16_t(frame[0]) | (uint16_t(frame[1]) << 8));
    memset(&data->info, 0, sizeof(data->info));
    auto table = static_cast<iwm_agn_scd_bc_tbl *>(sc->sched_dma.vaddr);
    const auto bc = htole16(ItlAwdl::auxUnencryptedSchedulerEntry(uint16_t(frameLength), IWM_AUX_STA_ID));
    table[qid].tfd_offset[idx] = bc;
    if (idx < IWM_TFD_QUEUE_SIZE_BC_DUP) table[qid].tfd_offset[IWM_TFD_QUEUE_SIZE_MAX + idx] = bc;
    ring->cur = (idx + 1) % IWM_TX_RING_COUNT; ++ring->queued;
    awdlProbeCounters->setRoc(C::TxQueue, qid); awdlProbeCounters->setRoc(C::TxIndex, idx);
    awdlProbeCounters->setRoc(C::TxSubmittedGp2, gp2); awdlProbeCounters->add(C::AuxTxSubmitted);
    awdlAuxTxPublish();
    if(awdlTemplateTxEnabled && !umiReady){
        auto &radio=ItlAwdl::gTemplateRadio;
        radio.pendingSingleData=singleData;
        if(!singleData){
        radio.pendingSubtype=frame[30];
        if(frame[30]==3)++radio.mifSubmitted;else ++radio.psfSubmitted;
        radio.lastLength=uint16_t(frameLength);memcpy(radio.lastFrame,frame,frameLength);
        radio.lastAw=input.clock.aw(input.gp2,input.sync);radio.lastDown=input.clock.down(input.gp2,input.sync);
        radio.lastTxGp2=input.gp2;radio.lastStartGp2=input.startGp2;radio.lastSkip=0;
        }
    }
    if(awdlTemplateTxEnabled || umiReady) ++awdlTemplateSequence;
    __sync_synchronize();
    IWM_WRITE(sc, IWM_HBUS_TARG_WRPTR, (qid << 8) | ring->cur);
    if(singleData)awdlSingleDataProbePublish();
#endif
}
bool ItlIwm::awdlAuxTxComplete(unsigned qid, unsigned idx, uint32_t status) {
    if (!awdlAuxTx.complete(com.sc_generation, qid, idx)) return false;
    using C = ItlAwdlProbeCounters;
    awdlProbeCounters->add(C::AuxTxCompleted);
    const uint32_t code = status & IWM_TX_STATUS_MSK;
    awdlProbeCounters->add(code == IWM_TX_STATUS_SUCCESS || code == IWM_TX_STATUS_DIRECT_DONE ? C::AuxTxSuccess : C::AuxTxFailed);
    awdlProbeCounters->setRoc(C::TxStatus, status);
    auto ring = &com.txq[qid];
    iwm_reset_sched(&com, qid, idx, IWM_AUX_STA_ID);
    iwm_txd_done(&com, &ring->data[idx]); --ring->queued;
    // Queue ownership is gone at this point. A failed frame must not stop the
    // bounded experiment; counters retain the failure while the next ROC gets
    // one fresh frame.
    if (gItlNativeUmi.owner == this && gItlNativeUmi.flight) {
        const bool success = code == IWM_TX_STATUS_SUCCESS || code == IWM_TX_STATUS_DIRECT_DONE;
        gItlNativeUmi.pending = gItlNativeUmi.flight = false;
        gItlNativeUmi.length = 0;
        gItlNativeUmi.deadlineNs = 0;
        if (success) ++gItlNativeUmi.completed; else ++gItlNativeUmi.failed;
        awdlNativeUmiPublish();
        awdlAuxTx.recycle();
    } else if (awdlTemplateTxEnabled) {
        if(ItlAwdl::gTemplateRadio.pendingSingleData){
            ItlAwdl::gSingleDataProbe.complete(com.sc_generation,qid,idx,status,code==IWM_TX_STATUS_SUCCESS);
            ItlAwdl::gTemplateRadio.pendingSingleData=false;awdlSingleDataProbePublish();
        }else if(ItlAwdl::gTemplateRadio.pendingSubtype==3)++ItlAwdl::gTemplateRadio.mifCompleted;
        else ++ItlAwdl::gTemplateRadio.psfCompleted;
        awdlAuxTx.recycle();
    }
    // No AP rate-control, node references or normal data-path watchdog ownership.
    awdlAuxTxPublish();
    if (awdlRocSession.phase == ItlAwdl::RocPhase::Draining) {
        auto &retired=ItlAwdl::gTemplateRadio;
        retired.retiredUid=awdlRocSession.uid;retired.retiredGeneration=awdlRocSession.generation;retired.retiredValid=true;
        awdlRocSession.queueDrained(); awdlRocPublish();
    }
    return true;
}
void ItlIwm::awdlAuxTxTick() {
    auto &probe=ItlAwdl::gSingleDataProbe;const auto phase=probe.phase;const auto reason=probe.reason;
    probe.tick(com.sc_generation,itlAwdlRocNowNs());
    const uint64_t now = itlAwdlRocNowNs();
    if (gItlNativeUmi.owner == this && !gItlNativeUmi.flight &&
        (gItlNativeUmi.pending || gItlNativeUmi.deadlineNs) &&
        now >= gItlNativeUmi.deadlineNs) {
        ++gItlNativeUmi.dropped;
        gItlNativeUmi.pending = false;
        gItlNativeUmi.length = 0;
        gItlNativeUmi.deadlineNs = 0;
        awdlNativeUmiPublish();
    }
    if(!awdlAuxTx.pending&&gItlNativeUmi.owner==this&&
       gItlNativeUmi.pending&&!gItlNativeUmi.flight)awdlAuxTxStart();
    if(awdlAuxTxEnabled&&!awdlAuxTx.pending&&probe.phase==ItlAwdl::SingleDataPhase::Armed)awdlAuxTxStart(true);
    if(probe.phase!=phase||probe.reason!=reason||probe.lastBlockReason!=probe.publishedBlockReason)awdlSingleDataProbePublish();
    const bool nativeUmi = gItlNativeUmi.owner == this &&
        (gItlNativeUmi.pending || gItlNativeUmi.flight);
    if ((!awdlAuxTxEnabled && !nativeUmi) || !awdlAuxTx.pending ||
        awdlAuxTx.generation != uint32_t(com.sc_generation)) return;
    using C = ItlAwdlProbeCounters;
    if (awdlAuxTx.flushSent) {
        if (awdlAuxTx.phase != ItlAwdl::AuxTxPhase::Quarantined && now > awdlAuxTx.deadlineNs) {
            awdlProbeCounters->add(C::AuxTxCleanupTimeouts); awdlAuxTx.quarantine(); awdlAuxTxPublish();
        }
        return;
    }
    if (awdlRocSession.phase == ItlAwdl::RocPhase::Running && now <= awdlAuxTx.deadlineNs) return;
    if (!awdlAuxTx.requestFlush(com.sc_generation, now + 2000000000ULL)) return;
    awdlProbeCounters->add(C::AuxTxFlushes); awdlAuxTxPublish();
    const int generation = com.sc_generation;
    const int err = iwm_flush_tx_path(&com, 1u << awdlAuxTx.queue);
    if (generation != com.sc_generation) return;
    if (err && awdlAuxTx.pending) {
        awdlProbeCounters->add(C::AuxTxFlushErrors); awdlAuxTx.quarantine(); awdlAuxTxPublish();
    }
    // ACK is not completion. Keep mbuf/bounce ownership until TX notification
    // (including FIFO_FLUSHED) or a real hardware stop quiesces DMA.
}
