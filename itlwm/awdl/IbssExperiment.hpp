// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "IbssRecoveryObservation.hpp"
#include "IbssWindowProbe.hpp"
namespace ItlIbss {
template<class Parser> bool bootExperimentArmed(Parser parser) {
    int value = 0;
    return parser("itlwm_ibss_probe",&value,sizeof(value)) && value == 1;
}
template<class Parser> bool bootProfileRequested(Parser parser) {
    int value=0;return parser("itlwm_ibss_profile",&value,sizeof(value)) && value==1;
}
// Independent observation only; this switch never arms OneShot or its timer.
template<class Parser> bool bootPassiveTraceRequested(Parser parser) {
    int value=0;return parser("itlwm_ibss_trace",&value,sizeof(value)) && value==1;
}
struct OneShot {
    bool armed = false, attempted = false, profile = false;
    uint32_t stableGeneration = 0;
    uint64_t stableSince = 0;
    bool due(uint64_t now, bool eligible, uint32_t generation, bool launchReady = true) {
        if (!armed || attempted) return false;
        if (!eligible || !generation) { stableSince = 0; stableGeneration = 0; return false; }
        if (stableGeneration != generation || !stableSince || now < stableSince) {
            stableGeneration = generation; stableSince = now ? now : 1; return false;
        }
        if (now - stableSince < 60000000000ull || !launchReady) return false;
        attempted = true; return true; // Consume BEFORE any operation; no auto retry.
    }
};
struct ExperimentRows {
    enum Field { Serial,Kind,HostUsLow,HostUsHigh,Generation,Phase,Code,Result,Flags,
        Channel,Gp2,MacContext,Status,Valid,Length,FrameControl,Detail0,Detail1,Count };
    enum Event { State = 1, CommandSubmit, CommandReturn, FirmwareStatus, RawRx,
                 CommonRx, AwdlEnvelope, BeaconNotification, WaitSubmit, WaitReturn, StepSubmit, StepReturn };
    // Bounded 120s receive validation must retain command and recovery evidence.
    static constexpr unsigned Capacity = 4096;
    struct Row { uint32_t v[Count] = {}; };
    struct Slot { uint32_t version = 0; Row row; } slots[Capacity];
    uint32_t writer = 0, cursor = 0, serial = 0, lost = 0;
    static const char *name(unsigned i) {
        const char *const names[] = {"Serial","Kind","HostUsLow","HostUsHigh","Generation",
            "Phase","Code","Result","Flags","Channel","Gp2","MacContext","Status","Valid",
            "Length","FrameControl","Detail0","Detail1"};
        static_assert(sizeof(names)/sizeof(names[0]) == Count,"IBSS row names");
        return i < Count ? names[i] : "Unknown";
    }
    void append(const Row &r) {
        uint32_t expected = 0;
        if (!__atomic_compare_exchange_n(&writer,&expected,1,false,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST)) {
            __atomic_add_fetch(&lost,1u,__ATOMIC_SEQ_CST); return;
        }
        if (cursor == Capacity) __atomic_add_fetch(&lost,1u,__ATOMIC_SEQ_CST);
        else {
            auto &slot = slots[cursor++];
            __atomic_add_fetch(&slot.version,1u,__ATOMIC_SEQ_CST);
            ++serial;
            for (unsigned i = 0; i < Count; ++i)
                __atomic_store_n(&slot.row.v[i],i == Serial ? serial : r.v[i],__ATOMIC_SEQ_CST);
            __atomic_add_fetch(&slot.version,1u,__ATOMIC_SEQ_CST);
        }
        __atomic_store_n(&writer,0u,__ATOMIC_SEQ_CST);
    }
    bool read(unsigned i, Row &out) const {
        out = {}; if (i >= Capacity) return false;
        auto &slot = slots[i]; uint32_t before = __atomic_load_n(&slot.version,__ATOMIC_SEQ_CST);
        if (!before || (before & 1)) return false;
        for (unsigned j = 0; j < Count; ++j) out.v[j] = __atomic_load_n(&slot.row.v[j],__ATOMIC_SEQ_CST);
        return before == __atomic_load_n(&slot.version,__ATOMIC_SEQ_CST) && out.v[Serial];
    }
};
struct ExperimentObservation {
    enum Metric { Armed,Attempted,Phase,BeginError,ConfigureError,ConfigurationCompleted,
        CaptureGeneration,TargetMac,RawIngress,RawChannel6,CommonFrames,CommonChannel6,
        ValidEnvelopes6,BeaconNotifications,BeaconDecoded,BeaconReportedSuccess,
        CommandSubmitted,CommandReturned,CommandErrors,OutOfGateRx,PrimaryError,RecoveryError,ProfileRequested,LifecycleEvents,LifecycleOffGate,CancelExpectedGeneration,CancelActualGeneration,CancelReason,PassiveTrace,FirmwareFaultTraceVersion,RecoveryObserverVersion,RecoveryState,RecoveryInitialGeneration,RecoveryGeneration,RecoveryGenerationChanges,RecoveryFaults,RecoverySampleCount,RecoveryStableMs,RecoveryGapCount,TargetChannel,RawTargetChannel,CommonTargetChannel,ValidEnvelopesTarget,NvmChannelFlagsVersion,NvmFlags6,NvmFlags44,NvmFlags149,MccProfileVersion,MccDecoded,MccGeneration,MccStatus,MccCountry,MccSource,MccChannelCount,MccFlags6,MccFlags44,MccFlags149,MccPresent,MccChubCount,MccChubCountry,MccChubSource,ResourceTraceVersion,ResourceTraceSubmitted,ResourceTraceReturned,ResourceTraceDecodeErrors,ResourceTraceOffGate,TxProbeVersion,TxAttempted,TxAccepted,TxSubmitted,TxCompleted,TxStatus,TxReclaimed,TxStopReclaimed,TxFlushRequested,TxFlushFailed,TxMismatch,TxCancelled,TxGeneration,TxRun,TxPayloadBytes,TxDeadlineExpired,Count };
    uint32_t metrics[Count] = {}, commandSerial = 0, captureArmed = 0;
    uint64_t startNs = 0, deadlineNs = 0;
    uint64_t lifecycleStartNs=0,lifecycleDeadlineNs=0;
    StaRecoveryObservation recovery;
    LateRecoveryAdmission lateAdmission;
    uint64_t recoveryStartNs=0,recoveryDeadlineNs=0;
    uint64_t clockLastNs=0;uint32_t clockSampleCount=0;
    WindowProbe window;
    // Diagnostic cadence is unchanged; only an explicit live native lease may use16ms/256 reads.
    uint32_t clockWaitUs(uint64_t now,bool native=false) const {
        if(clockSampleCount>=(native?256u:100u))return 250000;
        const auto last=__atomic_load_n(&clockLastNs,__ATOMIC_SEQ_CST);
        const uint64_t interval=native?16000000ull:200000000ull;
        if(!last || now<last || now-last>=interval)return 1;
        return uint32_t((interval-(now-last)+999)/1000);
    }
    bool claimClockSample(uint32_t generation,uint64_t now,bool force=false,bool native=false) {
        if(clockSampleCount>=(native?256u:100u) || !capturesProtocol(generation,now))return false;
        const auto last=__atomic_load_n(&clockLastNs,__ATOMIC_SEQ_CST);
        if(last && (now<last || (!force && now-last<(native?16000000ull:200000000ull))))return false;
        __atomic_store_n(&clockLastNs,now?now:1,__ATOMIC_SEQ_CST);++clockSampleCount;return true;
    }

    void armLifecycle(uint64_t now,uint64_t duration=120000000000ull) {
        __atomic_store_n(&lifecycleStartNs,now,__ATOMIC_SEQ_CST);
        __atomic_store_n(&lifecycleDeadlineNs,now+duration,__ATOMIC_SEQ_CST);
    }
    bool lifecycleCaptures(uint64_t now)const {
        return (get(Attempted) || get(PassiveTrace)) && now>=__atomic_load_n(&lifecycleStartNs,__ATOMIC_SEQ_CST) &&
            now<__atomic_load_n(&lifecycleDeadlineNs,__ATOMIC_SEQ_CST);
    }
    uintptr_t lifecycleThreads[16]={};
    uint32_t lifecycleThreadTag(uintptr_t thread) {
        if(!thread)return 0;
        for(unsigned i=0;i<16;++i) {
            auto old=__atomic_load_n(&lifecycleThreads[i],__ATOMIC_SEQ_CST);
            if(old==thread)return i+1;
            if(!old && __atomic_compare_exchange_n(&lifecycleThreads[i],&old,thread,false,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST))return i+1;
            if(old==thread)return i+1;
        }
        return 0; // Saturation is unknown identity, never export a pointer.
    }
    ExperimentRows control, rx;
    static const char *name(unsigned i) {
        const char *const names[] = {"Armed","Attempted","Phase","BeginError","ConfigureError",
            "ConfigurationCompleted","CaptureGeneration","TargetMac","RawIngress","RawChannel6",
            "CommonFrames","CommonChannel6","ValidEnvelopes6","BeaconNotifications","BeaconDecoded",
            "BeaconReportedSuccess","CommandSubmitted","CommandReturned","CommandErrors","OutOfGateRx","PrimaryError","RecoveryError","ProfileRequested","LifecycleEvents","LifecycleOffGate","CancelExpectedGeneration","CancelActualGeneration","CancelReason","PassiveTrace","FirmwareFaultTraceVersion","RecoveryObserverVersion","RecoveryState","RecoveryInitialGeneration","RecoveryGeneration","RecoveryGenerationChanges","RecoveryFaults","RecoverySampleCount","RecoveryStableMs","RecoveryGapCount","TargetChannel","RawTargetChannel","CommonTargetChannel","ValidEnvelopesTarget","NvmChannelFlagsVersion","NvmFlags6","NvmFlags44","NvmFlags149","MccProfileVersion","MccDecoded","MccGeneration","MccStatus","MccCountry","MccSource","MccChannelCount","MccFlags6","MccFlags44","MccFlags149","MccPresent","MccChubCount","MccChubCountry","MccChubSource","ResourceTraceVersion","ResourceTraceSubmitted","ResourceTraceReturned","ResourceTraceDecodeErrors","ResourceTraceOffGate","TxProbeVersion","TxAttempted","TxAccepted","TxSubmitted","TxCompleted","TxStatus","TxReclaimed","TxStopReclaimed","TxFlushRequested","TxFlushFailed","TxMismatch","TxCancelled","TxGeneration","TxRun","TxPayloadBytes","TxDeadlineExpired"};
        static_assert(sizeof(names)/sizeof(names[0]) == Count,"IBSS metric names");
        return i < Count ? names[i] : "Unknown";
    }
    void set(Metric m,uint32_t value) { __atomic_store_n(&metrics[m],value,__ATOMIC_SEQ_CST); }
    uint32_t get(Metric m) const { return __atomic_load_n(&metrics[m],__ATOMIC_SEQ_CST); }
    uint32_t add(Metric m) { return __atomic_add_fetch(&metrics[m],1u,__ATOMIC_SEQ_CST); }
    uint32_t nextCommand() { return __atomic_add_fetch(&commandSerial,1u,__ATOMIC_SEQ_CST); }
    bool arm(uint32_t generation,uint32_t mac,uint64_t start,uint64_t end) {
        if (!generation || end <= start || __atomic_load_n(&captureArmed,__ATOMIC_SEQ_CST)) return false;
        __atomic_store_n(&startNs,start,__ATOMIC_SEQ_CST);
        __atomic_store_n(&deadlineNs,end,__ATOMIC_SEQ_CST);
        set(CaptureGeneration,generation); set(TargetMac,mac);
        __atomic_store_n(&captureArmed,1u,__ATOMIC_SEQ_CST); return true;
    }
    bool capturesProtocol(uint32_t generation,uint64_t now)const {
        return get(ConfigurationCompleted) && captures(generation,now);
    }
    bool captures(uint32_t generation,uint64_t now) const {
        return __atomic_load_n(&captureArmed,__ATOMIC_SEQ_CST) && get(CaptureGeneration) == generation &&
            now >= __atomic_load_n(&startNs,__ATOMIC_SEQ_CST) && now < __atomic_load_n(&deadlineNs,__ATOMIC_SEQ_CST);
    }
};
struct ShortBeacon {
    bool valid = false; uint32_t status = 0, manager = 0, gp2 = 0;
    uint64_t tsf = 0;
};
inline ShortBeacon decodeShortBeacon(const uint8_t *p,size_t n,bool api46) {
    ShortBeacon out;
    if (!p || !api46 || n != 20) return out;
    out.valid = true;
    for (unsigned i = 0; i < 4; ++i) {
        out.status |= uint32_t(p[i]) << (8*i);
        out.manager |= uint32_t(p[12+i]) << (8*i);
        out.gp2 |= uint32_t(p[16+i]) << (8*i);
    }
    for (unsigned i = 0; i < 8; ++i) out.tsf |= uint64_t(p[4+i]) << (8*i);
    return out;
}
} // namespace ItlIbss
