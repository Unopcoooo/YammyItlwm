// SPDX-License-Identifier: GPL-2.0-or-later
// Included once by IWM's mac80211.cpp, after the ItlIwm declaration.
// Explicitly opted-in one-shot or bounded receive-only AUX ROC experiments.
#pragma once
#include "EngineState.hpp"
#include "TemplateRuntime.hpp"
#include "DataDriver.hpp"
#include "SingleDataProbe.hpp"
// The command sender releases the gate while sleeping. Keep the HAL, gate and
// workloop alive if detach runs during that sleep. Callers still reject a
// changed device generation before touching hardware or rearming callbacks.
struct ItlAwdlRocLifetime {
    OSObject *owner, *gate, *loop;
    ItlAwdlRocLifetime(OSObject *o, OSObject *g, OSObject *l) : owner(o), gate(g), loop(l) {
        if (owner) owner->retain(); if (gate) gate->retain(); if (loop) loop->retain();
    }
    ~ItlAwdlRocLifetime() {
        if (loop) loop->release(); if (gate) gate->release(); if (owner) owner->release();
    }
    ItlAwdlRocLifetime(const ItlAwdlRocLifetime &) = delete;
    ItlAwdlRocLifetime &operator=(const ItlAwdlRocLifetime &) = delete;
};
static uint64_t itlAwdlRocNowNs() {
    uint64_t uptime, ns;
    clock_get_uptime(&uptime); absolutetime_to_nanoseconds(uptime, &ns); return ns;
}
#include "DiscoveryTimerRuntime.hpp"
// Apply a software policy to the real IWM dwell loop under its own gate.
// This does not tune the infrastructure interface or enable AWDL synchronization.
IOReturn ItlIwm::setAwdlMasterChannel(uint32_t channel) {
    ItlAwdlRocLifetime lifetime(this, getMainCommandGate(), getMainWorkLoop());
    if (!getMainWorkLoop()->inGate()) {
        struct Request { ItlIwm *owner; uint32_t channel; } request = {this, channel};
        return getMainCommandGate()->runAction([](OSObject *, void *arg, void *, void *, void *) -> IOReturn {
            auto request = static_cast<Request *>(arg);
            return request->owner->setAwdlMasterChannel(request->channel);
        }, &request);
    }
    if (channel != 6 && channel != 44 && channel != 149)
        return kIOReturnBadArgument;
    if(awdlNativeSearchReserved()){
        uint32_t actual=0;
        if(!awdlNativeSearchMasterChannel(actual))return kIOReturnNotReady;
        return channel==actual?kIOReturnSuccess:kIOReturnUnsupported;
    }
    const uint64_t now = itlAwdlRocNowNs();
    if (!awdlProbeEnabled || !ItlAwdl::gEngine.modeEnabled || !ItlAwdl::gEngine.running ||
        !ItlAwdl::gEngine.deadlineNs || now >= uint64_t(ItlAwdl::gEngine.deadlineNs) ||
        com.sc_ic.ic_state != IEEE80211_S_RUN || com.ns_nstate != IEEE80211_S_RUN ||
        (com.sc_flags & (IWM_FLAG_SHUTDOWN | IWM_FLAG_HW_ERR | IWM_FLAG_RFKILL)) ||
        awdlRocSession.generation != uint32_t(com.sc_generation) ||
        awdlRocSession.phase == ItlAwdl::RocPhase::Quarantined)
        return kIOReturnNotReady;
    if (awdlPeerRocEngine) return kIOReturnUnsupported; // peer experiment owns its channel
    // This bounded backend serves only its explicitly chosen boot channel.
    // Do not silently retune an in-flight ROC or claim a different channel.
    if (channel != awdlRocChannel || channel != uint32_t(ItlAwdl::gEngine.channel))
        return kIOReturnUnsupported;
    const auto &entry = com.sc_ic.ic_channels[channel];
    if (!entry.ic_freq || !(entry.ic_flags & (IEEE80211_CHAN_2GHZ | IEEE80211_CHAN_5GHZ)) ||
        (entry.ic_flags & IEEE80211_CHAN_DFS))
        return kIOReturnBadArgument;
    awdlMasterChannel = channel;
    return kIOReturnSuccess; // policy installed; future ROC submissions consume it
}
void ItlIwm::awdlEngineAccountCycle() {
    if (awdlEngineCycleAccounted) return;
    awdlEngineCycleAccounted = true;
    ItlAwdl::gEngine.startsTotal += awdlRocSession.startCount;
    ItlAwdl::gEngine.endsTotal += awdlRocSession.endCount;
    ItlAwdl::gEngine.failuresTotal += awdlRocSession.failureCount;
    if (awdlRocSession.endCount) ++ItlAwdl::gEngine.cyclesCompleted;
    if (awdlRocSession.failureCount || (!awdlRocSession.startCount && !awdlRocSession.endCount))
        ++ItlAwdl::gEngine.cycleFailures;
}
// One coherent diagnostic row on the main gate. Kinds: 1 command, 2 response,
// 3 notification, 4 cancel, 5 quiesce. No frame payload or peer MAC is exported.
void ItlIwm::awdlPeerRocRecord(uint32_t kind, const uint8_t *notification, size_t length) {
    if (!awdlPeerRocEngine || !awdlProbeCounters || !awdlPeerRocAttempts) return;
    using H=ItlAwdl::RocHistory;
    H::Row row;
    const auto &p=awdlPeerRocPlan;const auto &s=awdlRocSession;
    row.v[H::Discovery]=awdlRocIsDiscovery;
    row.v[H::DurationTu]=awdlRocDurationTu;row.v[H::HomeChannel]=awdlRocHomeChannel;
    row.v[H::RxFrames]=awdlRocRxFrames;row.v[H::AwdlFrames]=awdlRocAwdlFrames;
    row.v[H::BeaconFrames]=awdlRocBeaconFrames;row.v[H::OffHomeFrames]=awdlRocOffHomeFrames;
    row.v[H::Attempt]=awdlPeerRocAttempts;row.v[H::Kind]=kind;
    row.v[H::PeerId]=p.peerId;row.v[H::Epoch]=p.epoch;
    row.v[H::Generation]=s.generation;row.v[H::Channel]=p.channel;
    row.v[H::BoundaryGp2]=p.boundaryGp2;row.v[H::PeriodUs]=p.periodUs;
    row.v[H::Slot]=p.slot;row.v[H::ObservationAgeUs]=p.observationAgeUs;
    row.v[H::Streak]=p.streak;row.v[H::ResidualRaw]=uint32_t(p.residualUs);
    row.v[H::PlanGp2]=p.boundaryGp2-p.waitUs;
    row.v[H::RequestedGp2]=awdlPeerRocRequestedGp2;row.v[H::MaxDelayTu]=awdlPeerRocDelayTu;
    row.v[H::Phase]=uint32_t(s.phase);row.v[H::Uid]=s.uid;
    row.v[H::StartGp2]=s.startGp2;row.v[H::EndGp2]=s.endGp2;
    row.v[H::Status]=s.lastStatus;row.v[H::ResponseStatus]=awdlPeerRocResponseStatus;
    if(s.startCount && !awdlRocIsDiscovery) {
        const auto a=ItlAwdl::measureRocAlignment(s.startGp2,s.endCount?s.endGp2:s.startGp2,p.boundaryGp2,p.periodUs);
        row.v[H::Before]=a.before;row.v[H::DistanceUs]=a.distanceUs;row.v[H::OverlapUs]=a.overlapUs;
    }
    // The raw command's station address is local MAC, not needed in diagnostics.
    // Zero its six address bytes while preserving all scheduling/wire fields.
    auto command=awdlPeerRocCommand;
    const size_t addressOffset=command.length==44 ? 24 : 20;
    for(unsigned i=0;i<6;++i) command.bytes[addressOffset+i]=0;
    for(unsigned i=0;i<11;++i) row.v[H::Command0+i]=ItlAwdl::rocRead32(command.bytes+4*i);
    row.v[H::NotificationLength]=uint32_t(length);
    if(notification && length==24)
        for(unsigned i=0;i<6;++i) row.v[H::Notification0+i]=ItlAwdl::rocRead32(notification+4*i);
    awdlProbeCounters->appendRocHistory(row);
}
void ItlIwm::awdlRocPublish() {
    if (!awdlProbeCounters) return;
    using C = ItlAwdlProbeCounters;
    auto &s = awdlRocSession;
    if(s.phase==ItlAwdl::RocPhase::Draining&&awdlAuxTxDrain()){
        auto &retired=ItlAwdl::gTemplateRadio;
        retired.retiredUid=s.uid;retired.retiredGeneration=s.generation;retired.retiredValid=true;
    }
    if(awdlTemplateTxEnabled&&getController()){
        auto &r=ItlAwdl::gTemplateRadio;
        getController()->setProperty("AWDLRocLastNotification",r.lastNotification,sizeof(r.lastNotification));
        getController()->setProperty("AWDLRocLastResponse",r.lastResponse,sizeof(r.lastResponse));
        getController()->setProperty("AWDLRocNotifyPhaseBefore",r.notifyPhaseBefore,32);
        getController()->setProperty("AWDLRocNotifyEarlyBefore",r.notifyEarlyBefore,32);
        getController()->setProperty("AWDLRocResponsePhaseBefore",r.responsePhaseBefore,32);
        getController()->setProperty("AWDLRocResponseEarlyBefore",r.responseEarlyBefore,32);
        getController()->setProperty("AWDLRocCurrentPhase",uint32_t(s.phase),32);
        getController()->setProperty("AWDLRocTargetGp2",r.targetGp2,32);
        getController()->setProperty("AWDLRocTargetRevision",r.targetRevision,32);
        getController()->setProperty("AWDLRocStartAligned",r.startAligned);
        getController()->setProperty("AWDLRocLastTxSkip",r.lastSkip,32);
        getController()->setProperty("AWDLRocNodeAddress",r.rocSource,sizeof(r.rocSource));
        getController()->setProperty("AWDLRocNodeValid",r.rocSourceValid);
        getController()->setProperty("AWDLRocNodeGeneration",r.rocSourceGeneration,32);
    }
    while (awdlRocRecordedStarts < s.startCount) { ++awdlRocRecordedStarts; awdlProbeCounters->add(C::RocStarts); }
    while (awdlRocRecordedEnds < s.endCount) { ++awdlRocRecordedEnds; awdlProbeCounters->add(C::RocEnds); }
    while (awdlRocRecordedFailures < s.failureCount) { ++awdlRocRecordedFailures; awdlProbeCounters->add(C::RocFailedNotifications); }
    awdlProbeCounters->setRoc(C::RocPhase, uint32_t(s.phase));
    if (awdlPeerRocEnabled && !awdlRocIsDiscovery && s.startCount && s.phase != ItlAwdl::RocPhase::Idle) {
        const auto a = ItlAwdl::measureRocAlignment(s.startGp2, s.endCount ? s.endGp2 : s.startGp2,
            awdlPeerRocPlan.boundaryGp2, awdlPeerRocPlan.periodUs);
        awdlProbeCounters->setRoc(C::RocStartBeforeBoundary, a.before);
        awdlProbeCounters->setRoc(C::RocStartDistanceUs, a.distanceUs);
        awdlProbeCounters->setRoc(C::RocWindowOverlapUs, a.overlapUs);
    }
    // Preserve the completed experiment's evidence after queueDrained() clears
    // the live UID/timestamp. Phase and reset count distinguish live state.
    if (s.phase != ItlAwdl::RocPhase::Idle) {
        awdlProbeCounters->setRoc(C::RocUid, s.uid);
        awdlProbeCounters->setRoc(C::RocStartGp2, s.startGp2);
        awdlProbeCounters->setRoc(C::RocEndGp2, s.endGp2);
        awdlProbeCounters->setRoc(C::RocStatus, s.lastStatus);
    }
}
void ItlIwm::awdlRocQuiesced() {
    // Only called after the device has been stopped and its queues reset.
    if (awdlRocAttempted && awdlProbeCounters) awdlProbeCounters->add(ItlAwdlProbeCounters::RocHardwareResets);
    awdlPeerRocRecord(5);
    awdlMasterChannel = 0;
    // Firmware initialization also stops the device, before the first tick.
    // Preserve only that pristine boot request. Once its window was armed or
    // a command attempted, a reset must never restart the old experiment.
    if (ItlAwdl::gEngine.modeEnabled &&
        (awdlRocAttempted || ItlAwdl::gEngine.deadlineNs ||
         ItlAwdl::gEngine.cyclesSubmitted || ItlAwdl::gEngine.running)) {
        ItlAwdl::gEngine.running = 0;
        ItlAwdl::gEngine.modeEnabled = 0;
    }
    ItlAwdl::gTemplateRadio.clock.reset();ItlAwdl::gTemplateRadio.targetValid=false;
    ItlAwdl::gTemplateRadio.startAligned=false;ItlAwdl::gTemplateRadio.nextWakeNs=0;
    ItlAwdl::gTemplateRadio.rocSourceValid=false;
    awdlRawRxWindow={};awdlRxControlHomeChannel=0;
    awdlRocSession.resetAfterDeviceQuiesced(com.sc_generation);
    awdlAuxTx.deviceQuiesced();
    itlNativeUmiQuiesce(this);
    ItlAwdl::gSingleDataProbe.quiesced();ItlAwdl::gTemplateRadio.pendingSingleData=false;
    if(ItlAwdl::gSingleDataProbe.everArmed)awdlSingleDataProbePublish();
    ItlAwdl::gDataPort.stop(true,ECANCELED); // Only after hardware stop and ring reset.
    awdlAuxTxPublish();
    awdlRocRecordedStarts = awdlRocRecordedEnds = awdlRocRecordedFailures = 0;
    awdlRocPublish(); // awdlRocAttempted intentionally survives device reset.
}
// The START can arrive before command response; deliver exactly once after
// either path has validated the UID/generation and applied buffered events.
void ItlIwm::awdlRocWindowReady() {
    using P=ItlAwdl::RocPhase;auto &radio=ItlAwdl::gTemplateRadio;
    if(awdlRocSession.phase==P::Running&&!radio.windowDelivered){
        radio.windowDelivered=true;
        if(awdlTemplateTxEnabled){
            ItlAwdl::TemplateSyncConfig sync;ItlAwdl::TemplateService service;uint32_t revision;
            radio.startAligned=radio.targetValid&&radio.targetGeneration==uint32_t(com.sc_generation)&&
                uint32_t(awdlRocSession.startGp2-radio.targetGp2)<=2000&&
                ItlAwdl::templateSnapshot(sync,service,revision)&&revision==radio.targetRevision&&
                ItlAwdl::templateIdentityValid(service,itlAwdlRocNowNs())&&
                radio.rocIdentityMatches(com.sc_generation,service.source);
            if(!radio.startAligned)radio.lastSkip=19;
        }
        awdlAuxTxStart();
        if(ItlAwdl::gEngine.availabilityWindowEvent&&(!awdlTemplateTxEnabled||radio.startAligned)){
            uint16_t aw=awdlTemplateTxEnabled?radio.targetAw:uint16_t(awdlRocSession.startGp2/16384u);
            ItlAwdl::gEngine.availabilityWindowEvent(ItlAwdl::gEngine.availabilityWindowContext,true,awdlRocSession.startGp2,aw);
        }
    }
    if(awdlRocSession.phase==P::Draining&&radio.windowDelivered&&!radio.endDelivered){
        radio.endDelivered=true;
        if(ItlAwdl::gEngine.availabilityWindowEvent&&(!awdlTemplateTxEnabled||radio.startAligned)){
            uint16_t aw=awdlTemplateTxEnabled?radio.targetAw:uint16_t(awdlRocSession.startGp2/16384u);
            ItlAwdl::gEngine.availabilityWindowEvent(ItlAwdl::gEngine.availabilityWindowContext,false,awdlRocSession.endGp2,aw);
        }
    }
}
void ItlIwm::awdlRocNotify(const uint8_t *p, size_t n) {
    if (!awdlRocAttempted || !awdlProbeCounters) return;
    using P = ItlAwdl::RocPhase;
    ItlAwdl::RocNotification raw={};
    const bool belongs=ItlAwdl::decodeRocNotification(p,n,raw) && raw.mac==4 &&
        (awdlRocSession.phase==P::Submitted || raw.uid==awdlRocSession.uid);
    auto &trace=ItlAwdl::gTemplateRadio;
    trace.notifyPhaseBefore=uint32_t(awdlRocSession.phase);trace.notifyEarlyBefore=awdlRocSession.earlyCount;
    if(n==24)memcpy(trace.lastNotification,p,24);
    const bool accepted = awdlRocSession.notify(p, n, com.sc_generation);
    if (!accepted) {
        awdlProbeCounters->add(ItlAwdlProbeCounters::RocIgnoredNotifications);
    }
    if(belongs&&accepted)awdlRocWindowReady();
    if (belongs) awdlPeerRocRecord(3,p,n);
    if (awdlRawRxEnabled && belongs && awdlRocSession.startCount &&
        (awdlRocSession.phase==P::Running || awdlRocSession.phase==P::Draining)) {
        auto &w=awdlRawRxWindow;
        w.generation=awdlRocSession.generation;w.channel=awdlRocChannel;
        w.start=awdlRocSession.startGp2;w.end=awdlRocSession.endGp2;
        w.duration=awdlRocDurationTu*1024u;w.valid=true;w.ended=awdlRocSession.endCount!=0;
    }
    awdlRocPublish();
    if (awdlRocSession.phase == P::Draining && awdlAuxTxDrain()) {
        awdlRocSession.queueDrained();
        awdlRocPublish();
    }
}
void ItlIwm::awdlRocCancel() {
    ItlAwdlRocLifetime lifetime(this, getMainCommandGate(), getMainWorkLoop());
    if (!getMainWorkLoop()->inGate()) {
        getMainCommandGate()->runAction([](OSObject *, void *arg, void *, void *, void *) -> IOReturn {
            static_cast<ItlIwm *>(arg)->awdlRocCancel(); return kIOReturnSuccess;
        }, this);
        return;
    }
    using C = ItlAwdlProbeCounters;
    if (!awdlProbeCounters || awdlRocCancelSent) return;
    if (awdlRocSession.generation != uint32_t(com.sc_generation)) return;
    if (!awdlRocSession.requestStop()) return;
    awdlRocCancelSent = true;
    awdlPeerRocRecord(4);
    awdlProbeCounters->add(C::RocCancels);
    const auto cmd = ItlAwdl::buildAuxRocRemove(isset(com.sc_enabled_capa, IWM_UCODE_TLV_CAPA_ULTRA_HB_CHANNELS), awdlRocSession.uid);
    const int generation = com.sc_generation;
    const int err = iwm_send_cmd_pdu(&com, IWM_HOT_SPOT_CMD, 0, cmd.length, cmd.bytes);
    if (generation != com.sc_generation) return;
    if (err) {
        awdlProbeCounters->add(C::RocCommandErrors);
        awdlRocSession.uncertainFailure();
    }
    awdlRocDeadlineNs = itlAwdlRocNowNs() + 2000000000ULL;
    awdlRocPublish(); // A command ACK is never interpreted as an END notification.
}
// Explicit post-login lease; never reset a fault-disabled engine or pending DMA.
IOReturn ItlIwm::awdlArmValidation(){
    ItlAwdlRocLifetime lifetime(this,getMainCommandGate(),getMainWorkLoop());
    if(!getMainWorkLoop()->inGate())
        return getMainCommandGate()->runAction([](OSObject*,void *arg,void*,void*,void*)->IOReturn{
            return static_cast<ItlIwm*>(arg)->awdlArmValidation();
        },this);
    if(awdlIbssDataActive())return kIOReturnNotReady;
    if(!awdlTemplateTxEnabled||!ItlAwdl::gEngine.modeEnabled||dmaDetaching||!awdlDiscoveryTimer)
        return kIOReturnNotReady;
    if(ItlAwdl::gTemplateRadio.validationArmed)return kIOReturnNotPermitted;
    auto &ring=com.txq[IWM_DQA_AUX_QUEUE];
    if(awdlRocSession.phase!=ItlAwdl::RocPhase::Idle||awdlAuxTx.pending||ring.queued||ring.cur!=ring.tail||
       awdlScanSubmitting||com.sc_ic.ic_state!=IEEE80211_S_RUN||
       (com.sc_flags&(IWM_FLAG_SHUTDOWN|IWM_FLAG_HW_ERR|IWM_FLAG_RFKILL|IWM_FLAG_SCANNING|IWM_FLAG_BGSCAN)))
        return kIOReturnBusy;
    ItlAwdl::gTemplateRadio.validationArmed=true;
    ItlAwdl::gEngine.deadlineNs=itlAwdlRocNowNs()+300000000000ULL;
    awdlDiscoveryTimer->setTimeoutMS(20);
    if(getController())getController()->setProperty("AWDLRuntimeValidationArmed",true);
    return kIOReturnSuccess;
}
void ItlIwm::awdlRocTick() {
    if (!awdlProbeEnabled || !awdlRocChannel || !awdlProbeCounters) return;
    ItlAwdlRocLifetime lifetime(this, getMainCommandGate(), getMainWorkLoop());
    if (!getMainWorkLoop()->inGate()) {
        getMainCommandGate()->runAction([](OSObject *, void *arg, void *, void *, void *) -> IOReturn {
            static_cast<ItlIwm *>(arg)->awdlRocTick(); return kIOReturnSuccess;
        }, this);
        return;
    }
    using C = ItlAwdlProbeCounters;
    using P = ItlAwdl::RocPhase;
    auto sc = &com; auto ic = &sc->sc_ic;
    // Template experiments require explicit authorization before any ROC/deadline.
    if(awdlTemplateTxEnabled&&!ItlAwdl::gTemplateRadio.validationArmed)return;
    if(awdlTemplateTxEnabled&&ItlAwdl::gEngine.running&&
       !ItlAwdl::gTemplateRadio.infraLost&&
       (ic->ic_state!=IEEE80211_S_RUN||sc->ns_nstate!=IEEE80211_S_RUN)){
        ItlAwdl::gTemplateRadio.infraLost=true;
        ItlAwdl::gEngine.deadlineNs=itlAwdlRocNowNs();
        ItlAwdl::gSingleDataProbe.cancel();awdlSingleDataProbePublish();
        if(getController())getController()->setProperty("AWDLRuntimeInfraLost",true);
    }
    awdlAuxTxTick();
    // Probe51: bounded repeating engine (5 minute window, see report). In engine mode the one-shot ROC code
    // below is re-armed every cycle until the five-minute window closes; the
    // controller layer only reports the interface's link as up while this engine
    // has actually completed dwell cycles.
    const bool engine = ItlAwdl::gEngine.modeEnabled != 0;
    bool engineExpired = false;
    if (engine) {
        const int64_t now = itlAwdlRocNowNs();
        if (!ItlAwdl::gEngine.deadlineNs)
            ItlAwdl::gEngine.deadlineNs = now + (awdlPeerRocEngine ? 600000000000LL : 300000000000LL);
        engineExpired = now >= ItlAwdl::gEngine.deadlineNs;
        if (engineExpired) {
            ItlAwdl::gEngine.running = 0;
            awdlMasterChannel = 0;
            // Stop admission, but keep servicing cancel/timeout/drain below.
            // ADD admission leaves a 200ms tail. Let finite dwells end naturally;
            // REMOVE acknowledgement does not promise a subsequent END event.
            if(!awdlTemplateTxEnabled&&awdlRocAttempted&&
               (awdlRocSession.phase==P::Submitted||awdlRocSession.phase==P::AwaitStart||awdlRocSession.phase==P::Running))
                awdlRocCancel();

        }
    }
    if (awdlRocAttempted) {
        const auto phase = awdlRocSession.phase;
        if (awdlRocSession.generation == uint32_t(sc->sc_generation) &&
            (phase == P::AwaitStart || phase == P::Running || phase == P::Stopping) &&
            itlAwdlRocNowNs() > awdlRocDeadlineNs) {
            awdlProbeCounters->add(C::RocTimeouts);
            if (phase == P::Stopping) { awdlRocSession.uncertainFailure(); awdlRocPublish(); }
            else awdlRocCancel();
        }
        if (engine && phase == P::Quarantined) {
            // Ambiguous firmware state: stop the engine rather than retrying.
            awdlEngineAccountCycle();
            ItlAwdl::gEngine.running = 0;
            awdlMasterChannel = 0;
            ItlAwdl::gEngine.modeEnabled = 0;
            return;
        }
        if (engine && phase == P::Idle && awdlRocSession.generation == uint32_t(sc->sc_generation)) {
            awdlEngineAccountCycle();
            if (engineExpired || (awdlPeerRocEngine && (awdlDiscoveryEnabled ? awdlDiscoveryPolicy.exhausted(awdlPeerRocAttempts) : awdlPeerRocAttempts>=24))) {
                ItlAwdl::gEngine.running=0;awdlMasterChannel=0;return;
            }
            ItlAwdl::gEngine.running = 1;
            awdlRocAttempted = false;
            awdlRocCancelSent = false;
            return;
        }
        return;
    }
    if (engineExpired || (awdlPeerRocEngine && (!engine || (awdlDiscoveryEnabled ? awdlDiscoveryPolicy.exhausted(awdlPeerRocAttempts) : awdlPeerRocAttempts>=24)))) return;
    // Wait at least ten normal 500 ms calibration callbacks after association.
    if (ic->ic_state != IEEE80211_S_RUN || ++awdlRocTicks < 10) return;
    if (awdlScanSubmitting || sc->ns_nstate != IEEE80211_S_RUN) return;
    if (sc->sc_flags & (IWM_FLAG_SHUTDOWN | IWM_FLAG_HW_ERR | IWM_FLAG_RFKILL |
                       IWM_FLAG_SCANNING | IWM_FLAG_BGSCAN | IWM_FLAG_TE_ACTIVE)) return;
    if (ic->ic_opmode != IEEE80211_M_STA || !ic->ic_bss) return;
    // Terminal admission failures never trigger repeated firmware attempts.
    if (!isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_HOTSPOT_SUPPORT)) {
        awdlRocAttempted = true; awdlProbeCounters->setRoc(C::RocSkipReason, 1); return;
    }
    const uint32_t dtimTu = uint32_t(ic->ic_bss->ni_dtimperiod) * ic->ic_bss->ni_intval;
    if (dtimTu < 32) { awdlProbeCounters->setRoc(C::RocSkipReason, 3); return; }
    uint32_t delayTu = MIN(dtimTu * 3, 600u);
    if (!iwm_nic_lock(sc)) { awdlProbeCounters->setRoc(C::RocSkipReason, 4); return; }
    uint32_t gp2 = iwm_read_prph(sc, IWM_DEVICE_SYSTEM_TIME_REG);
    iwm_nic_unlock(sc);
    if (awdlPeerRocEnabled) {
        ItlAwdl::PeerRocPlan plan={}; uint32_t reason=0;
        const bool planned=!awdlRxControlEnabled && awdlProbeCounters->planPeerRoc(sc->sc_generation,gp2,itlAwdlRocNowNs(),ic->ic_myaddr,plan,reason);
        awdlRocIsDiscovery=false;awdlRocDurationTu=16;
        if (!planned) {
            uint32_t channel=0,duration=0;
            if(!awdlDiscoveryEnabled || !awdlDiscoveryPolicy.candidate(itlAwdlRocNowNs(),dtimTu,channel,duration)) {
                awdlProbeCounters->setRoc(C::RocSkipReason,reason);return;
            }
            awdlRocIsDiscovery=true;awdlRocDurationTu=duration;
            if(awdlRxControlEnabled) {
                const uint32_t home=ieee80211_chan2ieee(ic,ic->ic_bss->ni_chan);
                if(!ItlAwdl::rxControlChannel(home,awdlDiscoveryPolicy.attempts,channel) ||
                   (awdlRxControlHomeChannel && awdlRxControlHomeChannel!=home)) {
                    ItlAwdl::gEngine.running=0;ItlAwdl::gEngine.modeEnabled=0;
                    awdlProbeCounters->setRoc(C::RocSkipReason,15);return;
                }
                if(!awdlRxControlHomeChannel)awdlRxControlHomeChannel=home;
            }
            plan={};plan.channel=uint8_t(channel); // no invented peer or timing confidence
        }
        awdlPeerRocPlan=plan;awdlRocChannel=plan.channel;
        // Paired-policy exploration: same admission/channel rules, alternating
        // delay budgets. A wide grant is not evidence of target-window overlap.
        if (!awdlRocIsDiscovery && (!awdlPeerRocEngine || !((awdlDiscoveryEnabled ? awdlDiscoveryPolicy.aligned : awdlPeerRocAttempts) & 1u))) delayTu=8;
        awdlProbeCounters->setRoc(C::RocChannel,plan.channel);
        awdlProbeCounters->setRoc(C::RocTargetBoundaryGp2,plan.boundaryGp2);
        awdlProbeCounters->setRoc(C::RocTargetPeriodUs,plan.periodUs);
        awdlProbeCounters->setRoc(C::RocPlanNowGp2,gp2);
        awdlProbeCounters->setRoc(C::RocObservationAgeUs,plan.observationAgeUs);
        awdlProbeCounters->setRoc(C::RocTargetSlot,plan.slot);
        awdlProbeCounters->setRoc(C::RocTimingStreak,plan.streak);
        awdlProbeCounters->setRoc(C::RocTimingResidualRaw,uint32_t(plan.residualUs));
    }
    if (engine && !awdlPeerRocEnabled && awdlMasterChannel)
        awdlRocChannel = awdlMasterChannel; // consume the configured AWDL policy
    const auto channel = &ic->ic_channels[awdlRocChannel];
    if (!channel->ic_freq || !(channel->ic_flags & (IEEE80211_CHAN_2GHZ | IEEE80211_CHAN_5GHZ)) ||
        (channel->ic_flags & IEEE80211_CHAN_DFS)) {
        awdlRocAttempted = true; awdlProbeCounters->setRoc(C::RocSkipReason, 2);
        if(awdlDiscoveryEnabled) { ItlAwdl::gEngine.running=0;ItlAwdl::gEngine.modeEnabled=0; }
        return;
    }
    if (awdlPeerRocEnabled && !awdlRocIsDiscovery) {
        if (!iwm_nic_lock(sc)) { awdlProbeCounters->setRoc(C::RocSkipReason,4);return; }
        gp2=iwm_read_prph(sc,IWM_DEVICE_SYSTEM_TIME_REG);iwm_nic_unlock(sc);
        const uint32_t lead=awdlPeerRocPlan.boundaryGp2-gp2;
        if (lead<2000 || lead>12000) { awdlProbeCounters->setRoc(C::RocSkipReason,14);return; }
    }
    if(awdlTemplateTxEnabled&&!awdlPeerRocEnabled){
        if(!ItlAwdl::templateLeaseAdmits(itlAwdlRocNowNs(),ItlAwdl::gEngine.deadlineNs))return;

        if(ItlAwdl::gTemplateRadio.nextWakeNs>itlAwdlRocNowNs())return;

        ItlAwdl::TemplateSyncConfig sync;ItlAwdl::TemplateService service;uint32_t revision;
        auto &radio=ItlAwdl::gTemplateRadio;const uint64_t now=itlAwdlRocNowNs();
        radio.nextWakeNs=0;
        if(ItlAwdl::templateSnapshot(sync,service,revision)&&ItlAwdl::templateScheduleValid(sync,6)){
            if(!radio.clock.valid(sc->sc_generation,revision,gp2,now)&&
               !radio.clock.initialize(sc->sc_generation,revision,gp2,now,sync))return;
            uint32_t target;uint16_t aw;
            if(!radio.clock.target(sync,gp2,target,aw))return;
            const int32_t until=int32_t(ItlAwdl::templateRequestWake(target)-gp2);
            if(until>0){radio.nextWakeNs=now+uint64_t(until)*1000;return;}
            if(int32_t(gp2-target)>2000)return;
            if(radio.alreadyRequested(sc->sc_generation,target)){
                const uint32_t end=ItlAwdl::templateRequestWake(target+uint32_t(sync.period)*sync.presence*1024u);
                radio.nextWakeNs=now+uint64_t(uint32_t(end-gp2))*1000;return;
            }

            radio.targetGp2=target;radio.targetAw=aw;radio.targetRevision=revision;
            radio.targetGeneration=sc->sc_generation;radio.targetValid=true;radio.startAligned=false;
            // Leave 6TU between consecutive requested dwells to avoid overlap.
            const uint32_t period=uint32_t(sync.period)*sync.presence;
            if(period<=6)return;
            awdlRocDurationTu=period-6;delayTu=5;
        }else{
            // One ordinary bootstrap ROC lets the family configure the native
            // interface. No template or AW event is emitted without a schedule.
            radio.targetValid=false;radio.startAligned=false;
            radio.nextWakeNs=now+500000000ULL;
        }
    }
    ItlAwdl::AuxRocCommand cmd;
    uint8_t node[6];ItlAwdl::TemplateSyncConfig nodeSync;ItlAwdl::TemplateService nodeIdentity;uint32_t nodeRevision;
    if(awdlTemplateTxEnabled&&!ItlAwdl::templateSnapshot(nodeSync,nodeIdentity,nodeRevision))return;
    if(!ItlAwdl::selectRocNodeAddress(awdlTemplateTxEnabled,ic->ic_myaddr,nodeIdentity,itlAwdlRocNowNs(),node))return;
    const bool extended = isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_ULTRA_HB_CHANNELS);
    if (!ItlAwdl::buildAuxRocAdd(extended, awdlRocChannel, IWM_AUX_STA_ID, node, gp2, awdlRocDurationTu, delayTu, cmd,
        awdlRxControlEnabled && awdlRocChannel==awdlRxControlHomeChannel ? uint8_t(awdlRxControlHomeChannel) : 0)) return;
    if(!awdlRocSession.prepareNext(sc->sc_generation))return;
    awdlRocAttempted = true; awdlRocCancelSent = false;
    awdlEngineCycleAccounted = false;
    // These cursors track publication within one session; the exposed counters
    // stay cumulative. Reset the cursors whenever the drained session is reused.
    awdlRocRecordedStarts = awdlRocRecordedEnds = awdlRocRecordedFailures = 0;
    if (!awdlRocSession.begin(sc->sc_generation)) return;
    memcpy(ItlAwdl::gTemplateRadio.rocSource,node,6);
    ItlAwdl::gTemplateRadio.rocSourceGeneration=sc->sc_generation;ItlAwdl::gTemplateRadio.rocSourceValid=true;
    ItlAwdl::gTemplateRadio.windowDelivered=false;ItlAwdl::gTemplateRadio.endDelivered=false;
    awdlRocHomeChannel=ieee80211_chan2ieee(ic,ic->ic_bss->ni_chan);
    awdlRocRxFrames=awdlRocAwdlFrames=awdlRocBeaconFrames=awdlRocOffHomeFrames=0;
    if (awdlPeerRocEngine) {
        if(awdlDiscoveryEnabled)awdlDiscoveryPolicy.submitted(itlAwdlRocNowNs(),awdlRocIsDiscovery);
        ++awdlPeerRocAttempts;awdlPeerRocCommand=cmd;
        awdlPeerRocRequestedGp2=gp2;awdlPeerRocDelayTu=delayTu;
        awdlPeerRocResponseStatus=UINT32_MAX;awdlPeerRocRecord(1);
    }
    awdlProbeCounters->setRoc(C::RocSkipReason, 0);
    awdlProbeCounters->setRoc(C::RocRequestedGp2, gp2);
    awdlProbeCounters->setRoc(C::RocDurationTu, awdlRocDurationTu);
    awdlProbeCounters->setRoc(C::RocMaxDelayTu, delayTu);
    awdlProbeCounters->setRoc(C::RocResponseStatus, UINT32_MAX); // no valid command response yet
    awdlProbeCounters->add(C::RocRequests);
    if (engine) {
        ++ItlAwdl::gEngine.cyclesSubmitted;
        ItlAwdl::gEngine.running = 1;
    }
    awdlRocPublish();
    const int generation = sc->sc_generation;
    struct iwm_host_cmd hcmd = {};
    hcmd.id = IWM_HOT_SPOT_CMD; hcmd.flags = IWM_CMD_WANT_RESP;
    hcmd.resp_pkt_len = sizeof(struct iwm_rx_packet) + 8;
    hcmd.data[0] = cmd.bytes; hcmd.len[0] = cmd.length;
    if(awdlTemplateTxEnabled&&ItlAwdl::gTemplateRadio.targetValid){
        auto &radio=ItlAwdl::gTemplateRadio;
        radio.requestedTargetValid=true;radio.requestedTarget=radio.targetGp2;
        radio.requestedGeneration=sc->sc_generation;
    }
    const int err = iwm_send_cmd(sc, &hcmd);
    // The normal command sender has a 2-second deadline and drops stale DMA epochs.
    if (generation != sc->sc_generation) { if (hcmd.resp_pkt) iwm_free_resp(sc, &hcmd); return; }
    if (err || !hcmd.resp_pkt || (hcmd.resp_pkt->hdr.flags & IWM_CMD_FAILED_MSK)) {
        awdlProbeCounters->add(C::RocCommandErrors); awdlRocSession.uncertainFailure();
        // Unknown UID cannot be removed safely. The requested dwell is finite;
        // quarantine the experiment, never submit another ADD or any AUX TX.
    } else {
        const size_t payloadLength = iwm_rx_packet_payload_len(hcmd.resp_pkt);
        if (payloadLength == 8) {
            awdlPeerRocResponseStatus=ItlAwdl::rocRead32(hcmd.resp_pkt->data + 4);
            awdlProbeCounters->setRoc(C::RocResponseStatus, awdlPeerRocResponseStatus);
        }
        auto &trace=ItlAwdl::gTemplateRadio;
        trace.responsePhaseBefore=uint32_t(awdlRocSession.phase);trace.responseEarlyBefore=awdlRocSession.earlyCount;
        if(payloadLength==8)memcpy(trace.lastResponse,hcmd.resp_pkt->data,8);
        if (awdlRocSession.response(hcmd.resp_pkt->data, payloadLength, generation,
            trace.retiredUid,trace.retiredValid&&trace.retiredGeneration==uint32_t(generation)))
            awdlProbeCounters->add(C::RocResponsesOk);
        else awdlProbeCounters->add(C::RocRejected);
    }
    awdlRocWindowReady();
    if(awdlRawRxEnabled && awdlRocSession.startCount &&
       (awdlRocSession.phase==P::Running || awdlRocSession.phase==P::Draining)) {
        auto &w=awdlRawRxWindow;w.generation=awdlRocSession.generation;w.channel=awdlRocChannel;
        w.start=awdlRocSession.startGp2;w.end=awdlRocSession.endGp2;w.duration=awdlRocDurationTu*1024u;
        w.valid=true;w.ended=awdlRocSession.endCount!=0;
    }
    awdlPeerRocRecord(2);
    if (hcmd.resp_pkt) iwm_free_resp(sc, &hcmd);
    awdlRocDeadlineNs = itlAwdlRocNowNs() + 2000000000ULL;
    awdlRocPublish();
    if (awdlRocSession.phase == P::Draining && awdlAuxTxDrain()) { awdlRocSession.queueDrained(); awdlRocPublish(); }
    // A state transition can occur while the synchronous command sleeps with
    // the gate released. Cancel the now-known UID if RUN was left in that gap.
    if (ic->ic_state != IEEE80211_S_RUN || sc->ns_nstate != IEEE80211_S_RUN) awdlRocCancel();
}
