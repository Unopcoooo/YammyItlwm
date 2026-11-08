// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <libkern/c++/OSObject.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSString.h>
#include <libkern/c++/OSSerialize.h>
#include <kern/clock.h>
#include "ProtocolModel.hpp"
#include "DataPolicy.hpp"
#include "TimingHistory.hpp"
#include "AfTxTiming.hpp"
#include "RocHistory.hpp"
#include "RawRxTrace.hpp"
#include "RawRxMetadata.hpp"
#include "IbssExperiment.hpp"
#include "IbssExperimentSerialize.hpp"
#include "IbssCausalEvidence.hpp"
// Independent property lifetime: no pointer back to HAL or controller.
// RX increments lock-free counters; allocation occurs only during a query.
class ItlAwdlProbeCounters : public OSObject {
    OSDeclareDefaultStructors(ItlAwdlProbeCounters)
public:
    ItlIbss::ExperimentObservation ibss;
    uint32_t batch[8]={}; // version, run, mode, phase, abort, archived, faults, max runs
    void setBatch(unsigned i,uint32_t value) { if(i<8)__atomic_store_n(&batch[i],value,__ATOMIC_SEQ_CST); }

    enum RocField { RocChannel, RocPhase, RocUid, RocRequestedGp2, RocStartGp2, RocEndGp2,
                    RocDurationTu, RocMaxDelayTu, RocStatus, RocSkipReason, RocResponseStatus,
                    TxEnabled, TxPhase, TxStatus, TxQueue, TxIndex, TxSubmittedGp2, PeerRocEnabled, RocTargetBoundaryGp2, RocTargetPeriodUs,
                    RocPlanNowGp2, RocObservationAgeUs, RocTargetSlot, RocTimingStreak, RocTimingResidualRaw,
                    RocStartBeforeBoundary, RocStartDistanceUs, RocWindowOverlapUs, RocFieldCount };
    void appendRawRx(const ItlAwdl::RawRxTrace::Row &row) { rawRxTrace.append(row); }
    void countRawRx(unsigned stage,unsigned bucket) {
        if(stage<ItlAwdl::RawStageCount && bucket<5)
            __atomic_fetch_add(&rawRxCounts[stage][bucket],uint64_t(1),__ATOMIC_RELAXED);
    }
    void appendRocHistory(const ItlAwdl::RocHistory::Row &row) { rocHistory.append(row); }
    void setRoc(RocField field, uint32_t value) { __atomic_store_n(&roc[field], value, __ATOMIC_RELAXED); }
    // Raw numbers from the most recent valid frame. Written on the RX workloop,
    // read during a property query: each field is stored atomically and the whole
    // set is explicitly not a simultaneous snapshot.
    enum LastField {
        LastChannel, LastNextChannel, LastMasterChannel, LastGuardTime,
        LastAwPeriod, LastAfPeriod, LastPresenceMode, LastNextAwSequence, LastTxDownCounter,
        LastMasterMetric, LastMasterCounter, LastHeight, LastSelfMetric, LastSelfCounter,
        LastChanSeqEncoding, LastChanSeqCount, LastChanSeqSteps, LastChanSeqDuplicates,
        LastChanSeqFill, LastChanSeqFirstChannel,
        LastChanSeqEntry0, LastChanSeqEntry1, LastChanSeqEntry2, LastChanSeqEntry3,
        LastChanSeqEntry4, LastChanSeqEntry5, LastChanSeqEntry6, LastChanSeqEntry7,
        LastChanSeqEntry8, LastChanSeqEntry9, LastChanSeqEntry10, LastChanSeqEntry11,
        LastChanSeqEntry12, LastChanSeqEntry13, LastChanSeqEntry14, LastChanSeqEntry15,
        LastFlags, LastAwExtensionLength, LastAwCommonLength, LastRemainingAwLength,
        LastMinExtension, LastMaxMulticastExtension, LastMaxUnicastExtension, LastMaxActionExtension,
        LastApAlignment, LastFrameLength, LastReceiveGp2,
        LastModelAccepted, LastModelPeerCount, LastModelPredictionValid,
        LastModelBoundaryGp2, LastModelWaitUs, LastModelSlot, LastModelChannel,
        LastTimingCompared, LastTimingConsistent, LastTimingStreak, LastTimingResidualRaw,
        LastSerial, LastFieldCount
    };
    bool init() override { return OSObject::init(); }
    void configureManagementFilter(bool enabled) { managementFilter = enabled; }
    // Freeze the first invalidating frame in the non-overwriting control prefix.
    void recordWindowInvalidation(uint32_t reason,uint32_t generation,uint32_t gp2,
                                  uint64_t host,uint32_t triggerPeer,const ItlAwdl::TimingHistory::Row *timing) {
        auto &w=ibss.window;
        if(!w.pending() || w.invalidated)return;
        w.invalidated=true;
        using E=ItlIbss::ExperimentRows;using T=ItlAwdl::TimingHistory;
        E::Row row;const auto us=host/1000;
        row.v[E::Kind]=36;row.v[E::Code]=1;row.v[E::Valid]=1;
        row.v[E::HostUsLow]=uint32_t(us);row.v[E::HostUsHigh]=uint32_t(us>>32);
        row.v[E::Generation]=generation;row.v[E::Gp2]=gp2;
        row.v[E::MacContext]=w.plan.peer;row.v[E::Length]=w.plan.epoch;
        row.v[E::Detail0]=w.plan.open;row.v[E::Detail1]=w.plan.close;
        row.v[E::Phase]=uint32_t(w.phase);row.v[E::Result]=reason;
        row.v[E::Flags]=triggerPeer;row.v[E::Status]=modelEpoch;
        row.v[E::FrameControl]=reason==1 ? pendingResetReason : timing->v[T::AuditReason];
        row.v[E::Channel]=__atomic_load_n(&last[LastChannel],__ATOMIC_RELAXED);
        ibss.control.append(row);
        if(timing) {
            row.v[E::Code]=2;
            row.v[E::Flags]=timing->v[T::Compared];row.v[E::Status]=timing->v[T::Consistent];
            row.v[E::FrameControl]=timing->v[T::Streak];row.v[E::Channel]=timing->v[T::ResidualRaw];
            row.v[E::Gp2]=timing->v[T::PreviousRxGp2];row.v[E::Result]=timing->v[T::GapUs];
            ibss.control.append(row);
        }
    }
    // RX workloop only. Never serialize peer addresses or publish a prediction
    // as current: it is calculated at this particular frame's reception time.
    void observeProtocol(const uint8_t *frame, size_t length, uint32_t gp2,
                         uint32_t generation, const uint8_t self[6]) {
        uint64_t uptime, nowNs;
        clock_get_uptime(&uptime);
        absolutetime_to_nanoseconds(uptime, &nowNs);
        const uint64_t hostGapNs=modelSeen && nowNs>=modelLastNs ? nowNs-modelLastNs : 0;
        const uint32_t resetReason=!modelSeen ? 1 : generation!=modelGeneration ? 2 :
            nowNs<modelLastNs ? 3 : hostGapNs>uint64_t(ItlAwdl::kPeerTimeoutUs)*1000 ? 4 : 0;
        // Host uptime rejects long silent gaps even if GP2 wrapped a full turn.
        if (!modelSeen || generation != modelGeneration || nowNs < modelLastNs ||
            nowNs - modelLastNs > uint64_t(ItlAwdl::kPeerTimeoutUs) * 1000) {
            ItlAwdl::peerTableReset(modelPeers);
            add(ModelResets);++modelEpoch;
            pendingResetReason=resetReason;
            pendingResetGapUs=hostGapNs/1000>UINT32_MAX ? UINT32_MAX : uint32_t(hostGapNs/1000);
        }
        modelSeen = true; modelGeneration = generation; modelLastNs = nowNs;
        if(ibss.window.pending() && modelEpoch!=ibss.window.plan.epoch)
            recordWindowInvalidation(1,generation,gp2,nowNs,0,nullptr);
        ItlIbss::CausalPrior causalPrior={};ItlAwdl::Summary causalEnvelope={};
        const bool causalMif=ibss.capturesProtocol(generation,nowNs) &&
            ItlAwdl::inspect(frame,length,causalEnvelope)==ItlAwdl::Result::Valid && causalEnvelope.subtype==3;
        if(causalMif) {
            const auto previous=ItlAwdl::peerTableFind(modelPeers,frame+10);
            if(previous)ItlIbss::freezeCausalPrior(*previous,generation,modelEpoch,gp2,causalPrior);
        }
        const auto status = ItlAwdl::ingestAdvertisement(modelPeers, self, frame, length, gp2);
        add(status == ItlAwdl::IngestResult::Accepted ? ModelAccepted :
            status == ItlAwdl::IngestResult::Full ? ModelFull :
            status == ItlAwdl::IngestResult::Self ? ModelSelf :
            status == ItlAwdl::IngestResult::Downgrade ? ModelDowngrade : ModelRejected);
        ItlAwdl::ProtocolPrediction prediction = {};
        const bool valid = ItlAwdl::predictProtocol(modelPeers, self, gp2, prediction);
        if (valid) add(ModelPredictions);
        auto peer = status == ItlAwdl::IngestResult::Accepted ? ItlAwdl::peerTableFind(modelPeers, frame+10) : nullptr;
        if (peer) {
            if (!peer->diagnosticId) {if (++diagnosticNextId==0)++diagnosticNextId;peer->diagnosticId=diagnosticNextId;}
            using H=ItlAwdl::TimingHistory;
            H::Row row;
            const auto &a=peer->timing;
            row.v[H::PeerId]=peer->diagnosticId;row.v[H::Epoch]=modelEpoch;row.v[H::ResetReason]=pendingResetReason;row.v[H::ResetGapUs]=pendingResetGapUs;
            row.v[H::AuditReason]=uint32_t(a.reason);row.v[H::Compared]=a.compared;row.v[H::Consistent]=a.consistent;
            row.v[H::Streak]=a.streak;row.v[H::HostGapUs]=hostGapNs/1000>UINT32_MAX ? UINT32_MAX : uint32_t(hostGapNs/1000);
            row.v[H::GapUs]=a.gapUs;row.v[H::PreviousRxGp2]=a.priorRxGp2;row.v[H::ReceiveGp2]=gp2;
            row.v[H::PreviousAwSequence]=a.priorAwSequence;row.v[H::AwSequence]=peer->sync.nextAwSequence;
            row.v[H::PreviousDownCounter]=a.priorDownCounter;row.v[H::DownCounter]=peer->sync.txDownCounter;
            row.v[H::PreviousAwPeriod]=a.priorAwPeriod;row.v[H::AwPeriod]=peer->sync.awPeriod;
            row.v[H::PreviousPresenceMode]=a.priorPresenceMode;row.v[H::PresenceMode]=peer->sync.presenceMode;
            row.v[H::PreviousBoundaryGp2]=a.oldBoundaryGp2;row.v[H::BoundaryGp2]=a.newBoundaryGp2;
            row.v[H::SequenceDeltaRaw]=a.sequenceDeltaRaw;row.v[H::ResidualRaw]=uint32_t(a.residualUs);
            row.v[H::RxChannel]=__atomic_load_n(&last[LastChannel],__ATOMIC_RELAXED);
            row.v[H::ActionSubtype]=frame[30+((frame[1]&0x80)?4:0)];
            row.v[H::ModelSlot]=prediction.slot.slot;row.v[H::ModelChannel]=prediction.slot.channel;
            row.v[H::ScheduleChangedSlot]=a.changedSlot;
            ItlAwdl::AfTxTiming wire;
            if(ItlAwdl::decodeAfTxTiming(frame,length,wire)) {
                row.v[H::AfPhyTx]=wire.phy;row.v[H::AfTargetTx]=wire.target;
                row.v[H::Dot11Sequence]=wire.sequence;row.v[H::FrameLength]=uint32_t(length);
            }
            row.v[H::HostUsLow]=uint32_t(nowNs/1000);row.v[H::HostUsHigh]=uint32_t((nowNs/1000)>>32);
            // Independent finite history freezes at the IBSS lease boundary;
            // ordinary scanning afterwards cannot overwrite this evidence.
            if(ibss.capturesProtocol(generation,nowNs)) ibssTimingHistory.append(row);
            if(ibss.window.pending() && (modelEpoch!=ibss.window.plan.epoch ||
               (peer->diagnosticId==ibss.window.plan.peer && !a.consistent)))
                recordWindowInvalidation(2,generation,gp2,nowNs,peer->diagnosticId,&row);
            ibss.window.peer(generation,peer->diagnosticId,modelEpoch,gp2);
            if(ibss.capturesProtocol(generation,nowNs)) {
                const auto code=ibss.window.claimEawAf(generation,peer->diagnosticId,modelEpoch,gp2,a.consistent);
                if(code) {
                    using E=ItlIbss::ExperimentRows;E::Row evidence;const auto us=nowNs/1000;
                    evidence.v[E::Kind]=32;evidence.v[E::Code]=code;evidence.v[E::Valid]=1;
                    evidence.v[E::Generation]=generation;evidence.v[E::MacContext]=peer->diagnosticId;evidence.v[E::Length]=modelEpoch;
                    evidence.v[E::Phase]=ibss.window.plan.open;evidence.v[E::Gp2]=gp2;
                    evidence.v[E::HostUsLow]=uint32_t(us);evidence.v[E::HostUsHigh]=uint32_t(us>>32);
                    evidence.v[E::Flags]=peer->diagnosticId==ibss.window.plan.peer;evidence.v[E::Status]=a.consistent;
                    evidence.v[E::Detail0]=ibss.window.plan.boundary;evidence.v[E::Detail1]=ibss.window.plan.period;
                    evidence.v[E::Channel]=__atomic_load_n(&last[LastChannel],__ATOMIC_RELAXED);
                    evidence.v[E::Result]=row.v[H::ActionSubtype];ibss.control.append(evidence);
                }
            }
            if(causalMif && ibss.capturesProtocol(generation,nowNs)) {
                ItlIbss::CausalView causalView={};
                if(ItlIbss::evaluateCausalPrior(causalPrior,generation,modelEpoch,peer->diagnosticId,gp2,causalView)) {
                    const unsigned sample=causalBudget.claim();
                    if(sample) {
                        const auto channel=__atomic_load_n(&last[LastChannel],__ATOMIC_RELAXED);
                        ibss.control.append(ItlIbss::causalEvidenceRow(causalPrior,causalView,gp2,channel,nowNs,sample,false,a.consistent));
                        if(sample<=8)ibss.control.append(ItlIbss::causalEvidenceRow(causalPrior,causalView,gp2,channel,nowNs,sample,true,a.consistent));
                    }
                }
            }
            timingHistory.append(row);pendingResetReason=pendingResetGapUs=0;
        }
        recordLastField(LastTimingCompared, peer && peer->timing.compared);
        recordLastField(LastTimingConsistent, peer && peer->timing.consistent);
        recordLastField(LastTimingStreak, peer ? peer->timing.streak : 0);
        recordLastField(LastTimingResidualRaw, peer ? uint32_t(peer->timing.residualUs) : 0);
        if (peer && peer->timing.compared) add(peer->timing.consistent ? TimingConsistent : TimingInconsistent);
        recordLastField(LastReceiveGp2, gp2);
        recordLastField(LastModelAccepted, status == ItlAwdl::IngestResult::Accepted);
        recordLastField(LastModelPeerCount, modelPeers.count);
        recordLastField(LastModelPredictionValid, valid);
        recordLastField(LastModelBoundaryGp2, prediction.window.nextBoundaryGp2);
        recordLastField(LastModelWaitUs, prediction.window.waitMicroseconds);
        recordLastField(LastModelSlot, prediction.slot.slot);
        recordLastField(LastModelChannel, prediction.slot.channel);
    }
    // Candidate software timing probe only. Existing IBSS radio stays on 6.
    bool planIbssWindow(uint32_t generation,uint32_t gp2,uint64_t host,uint64_t lease,
                        const uint8_t self[6],ItlIbss::WindowPlan &out,uint32_t &reason,const uint8_t *target=nullptr) {
        out={};reason=1;
        if(!modelSeen || generation!=modelGeneration || host<modelLastNs || host-modelLastNs>500000000ull)return false;
        ItlAwdl::ProtocolPrediction prediction={};reason=2;
        const ItlAwdl::Peer *peer=nullptr;
        if(target){
            peer=ItlAwdl::peerTableFind(modelPeers,target);
            if(!peer||!peer->hasSync||!peer->hasChannels||
               !ItlAwdl::predictWindowWithTxDelay(peer->sync,peer->syncReceivedGp2,gp2,500000,peer->timing.txDelayUs,prediction.window))return false;
        } else {
            if(!ItlAwdl::predictProtocol(modelPeers,self,gp2,prediction))return false;
            peer=ItlAwdl::peerTableFind(modelPeers,prediction.election.syncAddress);
        }
        if(!peer)return false;reason=3;
        if(!peer->timing.consistent || peer->timing.streak<3)return false;
        const uint32_t period=uint32_t(peer->sync.awPeriod)*peer->sync.presenceMode*1024;
        reason=4;
        for(unsigned i=0;i<8;++i) {
            auto w=prediction.window;w.nextBoundaryGp2+=i*period;
            w.nextBoundarySequence=uint16_t(w.nextBoundarySequence+i*peer->sync.presenceMode);
            ItlAwdl::SlotView slot={};if(!ItlAwdl::predictNextSlot(peer->sync,peer->channels,w,slot) || slot.channel!=6)continue;
            const uint32_t duration=period-4096;
            const uint32_t open=w.nextBoundaryGp2+2048,close=w.nextBoundaryGp2+period-2048;
            const auto lead=ItlIbss::windowDelta(open,gp2);
            if(lead<20000 || lead>250000 || uint64_t(gp2-peer->syncReceivedGp2)+uint64_t(lead)+duration>500000)continue;
            if(host+uint64_t(lead+duration+5000)*1000>=lease){reason=5;return false;}
            out.version=2;out.generation=generation;out.peer=peer->diagnosticId;out.epoch=modelEpoch;
            out.boundary=w.nextBoundaryGp2;out.period=period;out.streak=peer->timing.streak;
            out.observedGp2=peer->syncReceivedGp2;out.observedHost=modelLastNs;out.open=open;out.close=close;
            out.rawAwSequence=peer->sync.nextAwSequence;out.txDelayUs=peer->timing.txDelayUs;
            out.awPeriod=peer->sync.awPeriod;out.presence=peer->sync.presenceMode;out.downCounter=peer->sync.txDownCounter;
            out.boundarySequence=w.nextBoundarySequence;out.selectedSlot=slot.slot;out.candidateOffset=i;
            out.sequenceCount=peer->channels.count;out.sequenceSteps=peer->channels.steps;out.sequenceFill=peer->channels.fill;
            out.sequenceEncoding=peer->channels.encoding;out.sequenceDuplicates=peer->channels.duplicates;
            for(unsigned j=0;j<16;++j)out.sequenceEntries[j]=uint16_t(peer->channels.entries[j].channel)|(uint16_t(peer->channels.entries[j].auxiliary)<<8);
            reason=0;return true;
        }
        return false;
    }
    // Borrowed pointers: caller must hold the HAL main work-loop gate.
    void dataModelView(ItlAwdl::DataContext &out) const {
        out.peers=modelSeen ? &modelPeers : nullptr;
        out.modelGeneration=modelGeneration;out.modelEpoch=modelEpoch;out.modelHostNs=modelLastNs;
    }
    // Main work-loop gate only, same serialization as RX. Never read atomically
    // exported scalar fields back as a peer model or authorization.
    bool planPeerRoc(uint32_t generation, uint32_t gp2, uint64_t nowNs, const uint8_t self[6],
                     ItlAwdl::PeerRocPlan &out, uint32_t &reason) {
        out={};reason=11;
        if (!modelSeen || generation != modelGeneration || nowNs < modelLastNs || nowNs-modelLastNs > 500000000ULL) return false;
        ItlAwdl::ProtocolPrediction prediction={};
        if (!ItlAwdl::predictProtocol(modelPeers,self,gp2,prediction)) return false;
        const auto peer=ItlAwdl::peerTableFind(modelPeers,prediction.election.syncAddress);
        if (!peer) return false;
        if (!ItlAwdl::planImminentPeerRoc(peer->timing,peer->sync,peer->channels,peer->syncReceivedGp2,gp2,out,reason)) return false;
        out.peerId=peer->diagnosticId;out.epoch=modelEpoch;return true;
    }
    void recordLastField(LastField field, uint32_t value) {
        __atomic_store_n(&last[field], value, __ATOMIC_RELAXED);
    }
    // Publishes the frame as complete: readers use the serial to tell whether a
    // snapshot was taken mid-update.
    void finishLastFrame() { __atomic_add_fetch(&last[LastSerial], uint32_t(1), __ATOMIC_RELAXED); }
    // Classify the frame's transmitter against the previous one without keeping a
    // table of addresses: the address itself is never serialized or exposed.
    void noteSender(const uint8_t *address) {
        const uint32_t low = uint32_t(address[0]) | (uint32_t(address[1]) << 8) |
                             (uint32_t(address[2]) << 16) | (uint32_t(address[3]) << 24);
        const uint32_t high = uint32_t(address[4]) | (uint32_t(address[5]) << 8);
        const bool same = haveSender &&
            __atomic_load_n(&previousSender[0], __ATOMIC_RELAXED) == low &&
            __atomic_load_n(&previousSender[1], __ATOMIC_RELAXED) == high;
        add(same ? SameSenderAsPrevious : DifferentSenderAsPrevious);
        __atomic_store_n(&previousSender[0], low, __ATOMIC_RELAXED);
        __atomic_store_n(&previousSender[1], high, __ATOMIC_RELAXED);
        haveSender = true;
    }
    enum Counter { RxFrames, ActionFrames, ValidAwdl, InvalidAction,
                   UnsupportedAction, OtherAction, SplitBuffer, Oversize,
                   MgmtFilterCmdsCompleted, MgmtFilterCmdErrors, AwdlDuringRun, AwdlOutsideRun,
                   AwdlChannel6, AwdlChannel44, AwdlChannel149, AwdlOtherChannel,
                   TlvSync, TlvElectionV1, TlvElectionV2, TlvChanSeq, TlvOther,
                   SyncDecodeOk, SyncDecodeErrors,
                   ElectionDecodeOk, ElectionDecodeErrors,
                   ChanSeqDecodeOk, ChanSeqDecodeErrors,
                   ElectionV2Frames, ElectionV1OnlyFrames, ElectionBothFrames, ElectionNoneFrames,
                   SyncWithChanSeqFrames,
                   ChanSeqCount16, ChanSeqCountOther,
                   ChanSeqEncodingSimple, ChanSeqEncodingLegacy, ChanSeqEncodingOpclass,
                   ChanSeqEncodingUnsupported,
                   SyncNextChannel6, SyncNextChannel44, SyncNextChannel149, SyncNextChannelOther,
                   SameSenderAsPrevious, DifferentSenderAsPrevious,
                   ModelAccepted, ModelRejected, ModelFull, ModelSelf, ModelDowngrade, ModelResets, ModelPredictions,
                   RocRequests, RocResponsesOk, RocCommandErrors, RocRejected,
                   RocStarts, RocEnds, RocFailedNotifications, RocTimeouts, RocCancels,
                   RocIgnoredNotifications, RocHardwareResets, RocFramesOnChannel, RocAwdlFramesOnChannel,
                   AuxTxAttempts, AuxTxSubmitted, AuxTxCompleted, AuxTxSuccess, AuxTxFailed,
                   AuxTxFlushes, AuxTxFlushErrors, AuxTxCleanupTimeouts, AuxTxSkipped, AuxTxResourceErrors, TimingConsistent, TimingInconsistent,
                   Count };
    void add(Counter c) { __atomic_fetch_add(&values[c], uint64_t(1), __ATOMIC_RELAXED); }
    bool serialize(OSSerialize *serializer) const override {
        static const char *const names[] = {
            "RxFrames", "ActionFrames", "ValidAwdl", "InvalidAction",
            "UnsupportedAction", "OtherAction", "SplitBuffer", "Oversize",
            "MgmtFilterCmdsCompleted", "MgmtFilterCmdErrors", "AwdlDuringRun", "AwdlOutsideRun",
            "AwdlChannel6", "AwdlChannel44", "AwdlChannel149", "AwdlOtherChannel",
            "TlvSync", "TlvElectionV1", "TlvElectionV2", "TlvChanSeq", "TlvOther",
            "SyncDecodeOk", "SyncDecodeErrors",
            "ElectionDecodeOk", "ElectionDecodeErrors",
            "ChanSeqDecodeOk", "ChanSeqDecodeErrors",
            "ElectionV2Frames", "ElectionV1OnlyFrames", "ElectionBothFrames", "ElectionNoneFrames",
            "SyncWithChanSeqFrames",
            "ChanSeqCount16", "ChanSeqCountOther",
            "ChanSeqEncodingSimple", "ChanSeqEncodingLegacy", "ChanSeqEncodingOpclass",
            "ChanSeqEncodingUnsupported",
            "SyncNextChannel6", "SyncNextChannel44", "SyncNextChannel149", "SyncNextChannelOther",
            "SameSenderAsPrevious", "DifferentSenderAsPrevious",
            "ModelAccepted", "ModelRejected", "ModelFull", "ModelSelf", "ModelDowngrade", "ModelResets", "ModelPredictions",
            "RocRequests", "RocResponsesOk", "RocCommandErrors", "RocRejected",
            "RocStarts", "RocEnds", "RocFailedNotifications", "RocTimeouts", "RocCancels",
            "RocIgnoredNotifications", "RocHardwareResets", "RocFramesOnChannel", "RocAwdlFramesOnChannel",
            "AuxTxAttempts", "AuxTxSubmitted", "AuxTxCompleted", "AuxTxSuccess", "AuxTxFailed",
            "AuxTxFlushes", "AuxTxFlushErrors", "AuxTxCleanupTimeouts", "AuxTxSkipped", "AuxTxResourceErrors", "TimingConsistent", "TimingInconsistent"
        };
        static_assert(sizeof(names) / sizeof(names[0]) == Count, "counter name table out of sync");
        auto dict = OSDictionary::withCapacity(Count + 1);
        if (!dict) return false;
        auto mode = OSString::withCString(managementFilter ? "rx-peer-window-history-mgmt-v32" : "rx-peer-window-history-v32");
        bool ok = mode && dict->setObject("Mode", mode);
        if (mode) mode->release();
        for (unsigned i = 0; ok && i < Count; ++i) {
            auto number = OSNumber::withNumber(__atomic_load_n(&values[i], __ATOMIC_RELAXED), 64);
            ok = number && dict->setObject(names[i], number);
            if (number) number->release();
        }
        if (ok) {
            static const char *const lastNames[] = {
                "Channel", "NextChannel", "MasterChannel", "GuardTime",
                "AwPeriod", "AfPeriod", "PresenceMode", "NextAwSequence", "TxDownCounter",
                "MasterMetric", "MasterCounter", "Height", "SelfMetric", "SelfCounter",
                "ChanSeqEncoding", "ChanSeqCount", "ChanSeqSteps", "ChanSeqDuplicates",
                "ChanSeqFill", "ChanSeqFirstChannel",
                "ChanSeqEntry0", "ChanSeqEntry1", "ChanSeqEntry2", "ChanSeqEntry3",
                "ChanSeqEntry4", "ChanSeqEntry5", "ChanSeqEntry6", "ChanSeqEntry7",
                "ChanSeqEntry8", "ChanSeqEntry9", "ChanSeqEntry10", "ChanSeqEntry11",
                "ChanSeqEntry12", "ChanSeqEntry13", "ChanSeqEntry14", "ChanSeqEntry15",
                "Flags", "AwExtensionLength", "AwCommonLength", "RemainingAwLength",
                "MinExtension", "MaxMulticastExtension", "MaxUnicastExtension", "MaxActionExtension",
                "ApAlignment", "FrameLength", "ReceiveGp2",
                "ModelAccepted", "ModelPeerCount", "ModelPredictionValid",
                "ModelBoundaryGp2", "ModelWaitUs", "ModelSlot", "ModelChannel",
                "TimingCompared", "TimingConsistent", "TimingStreak", "TimingResidualRaw", "Serial"
            };
            static_assert(sizeof(lastNames) / sizeof(lastNames[0]) == LastFieldCount,
                          "last-frame name table out of sync");
            auto lastDict = OSDictionary::withCapacity(LastFieldCount);
            ok = lastDict && dict->setObject("LastFrame", lastDict);
            for (unsigned i = 0; ok && i < LastFieldCount; ++i) {
                auto number = OSNumber::withNumber(__atomic_load_n(&last[i], __ATOMIC_RELAXED), 32);
                ok = number && lastDict->setObject(lastNames[i], number);
                if (number) number->release();
            }
            if (lastDict) lastDict->release();
        }
        if (ok) {
            static const char *const rocNames[] = {"Channel", "Phase", "Uid", "RequestedGp2", "StartGp2", "EndGp2",
                "DurationTu", "MaxDelayTu", "Status", "SkipReason", "ResponseStatus",
                "TxEnabled", "TxPhase", "TxStatus", "TxQueue", "TxIndex", "TxSubmittedGp2", "PeerRocEnabled", "TargetBoundaryGp2", "TargetPeriodUs",
                "PlanNowGp2", "ObservationAgeUs", "TargetSlot", "TimingStreak", "TimingResidualRaw",
                "StartBeforeBoundary", "StartDistanceUs", "WindowOverlapUs"};
            static_assert(sizeof(rocNames) / sizeof(rocNames[0]) == RocFieldCount, "ROC field names");
            auto rd = OSDictionary::withCapacity(RocFieldCount);
            ok = rd && dict->setObject("RocExperiment", rd);
            for (unsigned i = 0; ok && i < RocFieldCount; ++i) {
                auto number = OSNumber::withNumber(__atomic_load_n(&roc[i], __ATOMIC_RELAXED), 32);
                ok = number && rd->setObject(rocNames[i], number);
                if (number) number->release();
            }
            if (rd) rd->release();
        }
        if (ok) {
            using H=ItlAwdl::TimingHistory;
            static const char *const historyNames[]={
                "Serial","PeerId","Epoch","ResetReason","AuditReason","Compared","Consistent","Streak",
                "HostGapUs","ResetGapUs","GapUs","PreviousRxGp2","ReceiveGp2","PreviousAwSequence","AwSequence",
                "PreviousDownCounter","DownCounter","PreviousAwPeriod","AwPeriod","PreviousPresenceMode","PresenceMode",
                "PreviousBoundaryGp2","BoundaryGp2","SequenceDeltaRaw","ResidualRaw","RxChannel","ActionSubtype",
                "ModelSlot","ModelChannel","ScheduleChangedSlot","AfPhyTx","AfTargetTx","Dot11Sequence","FrameLength","HostUsLow","HostUsHigh"};
            static_assert(sizeof(historyNames)/sizeof(historyNames[0])==H::Count,"history field names");
            for(unsigned history=0;ok && history<2;++history) {
            auto rows=OSArray::withCapacity(H::Capacity);
            ok=rows && dict->setObject(history ? "IbssTimingHistory" : "TimingHistory",rows);
            for(unsigned i=0;ok && i<H::Capacity;++i) {
                H::Row snapshot;
                if(!(history ? ibssTimingHistory : timingHistory).read(i,snapshot))continue;
                auto row=OSDictionary::withCapacity(H::Count);
                ok=row;
                for(unsigned j=0;ok && j<H::Count;++j) {
                    auto number=OSNumber::withNumber(snapshot.v[j],32);
                    ok=number && row->setObject(historyNames[j],number);
                    if(number)number->release();
                }
                if(ok)ok=rows->setObject(row);
                if(row)row->release();
            }
            if(rows)rows->release();
            }
            auto version=OSNumber::withNumber(2,32);
            if(ok)ok=version && dict->setObject("IbssTimingHistoryVersion",version);
            if(version)version->release();
            auto model=OSNumber::withNumber(2,32);
            if(ok)ok=model && dict->setObject("IbssTimingModelVersion",model);
            if(model)model->release();
        }
        if (ok) {
            using H=ItlAwdl::RocHistory;
            auto rows=OSArray::withCapacity(H::Capacity);
            ok=rows && dict->setObject("RocHistory",rows);
            for(unsigned i=0;ok && i<H::Capacity;++i) {
                H::Row snapshot;
                if(!rocHistory.read(i,snapshot))continue;
                auto row=OSDictionary::withCapacity(H::Count);
                ok=row;
                for(unsigned j=0;ok && j<H::Count;++j) {
                    auto number=OSNumber::withNumber(snapshot.v[j],32);
                    ok=number && row->setObject(H::fieldName(j),number);
                    if(number)number->release();
                }
                if(ok)ok=rows->setObject(row);
                if(row)row->release();
            }
            if(rows)rows->release();
        }
        if(ok) {
            using H=ItlAwdl::RawRxTrace;
            auto rows=OSArray::withCapacity(H::Capacity);
            ok=rows && dict->setObject("RawRxTrace",rows);
            for(unsigned i=0;ok && i<H::Capacity;++i) {
                H::Row snapshot;if(!rawRxTrace.read(i,snapshot))continue;
                auto row=OSDictionary::withCapacity(H::Count);ok=row;
                for(unsigned j=0;ok && j<H::Count;++j) {
                    auto number=OSNumber::withNumber(snapshot.v[j],32);
                    ok=number && row->setObject(H::fieldName(j),number);
                    if(number)number->release();
                }
                if(ok)ok=rows->setObject(row);
                if(row)row->release();
            }
            if(rows)rows->release();
        }
        if(ok) {
            auto rows=OSArray::withCapacity(ItlAwdl::RawStageCount*5);
            ok=rows && dict->setObject("RawRxTotals",rows);
            const unsigned channels[]={0,6,44,149,UINT32_MAX};
            for(unsigned stage=0;ok && stage<ItlAwdl::RawStageCount;++stage)
                for(unsigned bucket=0;ok && bucket<5;++bucket) {
                    auto value=__atomic_load_n(&rawRxCounts[stage][bucket],__ATOMIC_RELAXED);
                    if(!value)continue;
                    auto row=OSDictionary::withCapacity(3);ok=row;
                    const uint64_t values[]={stage,channels[bucket],value};
                    const char *const names[]={"Stage","Channel","Count"};
                    for(unsigned n=0;ok && n<3;++n) {
                        auto number=OSNumber::withNumber(values[n],64);
                        ok=number && row->setObject(names[n],number);
                        if(number)number->release();
                    }
                    if(ok)ok=rows->setObject(row);
                    if(row)row->release();
                }
            if(rows)rows->release();
        }
        const char *batchNames[]={"BatchVersion","BatchRun","BatchMode","BatchPhase","BatchAbort","BatchArchived","BatchFaults","BatchMaxRuns"};
        for(unsigned i=0;i<8 && ok;++i) {
            auto n=OSNumber::withNumber(__atomic_load_n(&batch[i],__ATOMIC_SEQ_CST),32);
            ok=n && dict->setObject(batchNames[i],n); if(n)n->release();
        }
        if (ok) ok = serializeIbssExperiment(dict,ibss);
        if (ok) ok = dict->serialize(serializer);
        dict->release();
        return ok;
    }
private:
    ItlAwdl::PeerTable modelPeers = {};
    ItlIbss::CausalBudget causalBudget;
    ItlAwdl::TimingHistory timingHistory;
    ItlAwdl::TimingHistory ibssTimingHistory;
    ItlAwdl::RocHistory rocHistory;
    ItlAwdl::RawRxTrace rawRxTrace;
    alignas(8) uint64_t rawRxCounts[ItlAwdl::RawStageCount][5]={};
    uint32_t modelEpoch=0,diagnosticNextId=0,pendingResetReason=0,pendingResetGapUs=0;
    bool modelSeen = false;
    uint32_t modelGeneration = 0;
    uint64_t modelLastNs = 0;
    bool managementFilter = false;
    bool haveSender = false;
    alignas(8) uint32_t previousSender[2] = {};
    alignas(8) uint64_t values[Count] = {};
    alignas(8) uint32_t roc[RocFieldCount] = {};
    alignas(8) uint32_t last[LastFieldCount] = {};
};
