// Exercise the actual runtime bridge with a deterministic fake firmware/HAL.
// No macOS kernel or radio calls. This is not hardware validation.
#include "../../itlwm/awdl/AuxRoc.hpp"
#include "../../itlwm/awdl/AuxTx.hpp"
#include "../../itlwm/awdl/TimingAudit.hpp"
#include "../../itlwm/awdl/EngineState.hpp"
#include "../../itlwm/awdl/RocHistory.hpp"
#include "../../itlwm/awdl/DiscoveryPolicy.hpp"
ItlAwdl::EngineState ItlAwdl::gEngine = {};
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
using IOReturn = int;
struct OSObject { unsigned references=1; void retain() { ++references; } void release() { assert(references); --references; } };
constexpr int kIOReturnSuccess = 0;
constexpr int kIOReturnBadArgument=int32_t(0xe00002c2u), kIOReturnNotReady=int32_t(0xe00002d8u), kIOReturnUnsupported=int32_t(0xe00002c7u);
#define MIN(a,b) ((a)<(b)?(a):(b))
#define isset(p,b) (((p)[(b)/8] & (1u<<((b)%8))) != 0)
constexpr unsigned IWM_FLAG_SHUTDOWN=1, IWM_FLAG_HW_ERR=2, IWM_FLAG_RFKILL=4,
    IWM_FLAG_SCANNING=8, IWM_FLAG_BGSCAN=16, IWM_FLAG_TE_ACTIVE=32;
constexpr unsigned IWM_UCODE_TLV_CAPA_HOTSPOT_SUPPORT=18, IWM_UCODE_TLV_CAPA_ULTRA_HB_CHANNELS=48;
constexpr unsigned IEEE80211_S_RUN=1, IEEE80211_M_STA=1, IEEE80211_CHAN_2GHZ=1,
    IEEE80211_CHAN_5GHZ=2, IEEE80211_CHAN_DFS=4;
constexpr unsigned IWM_AUX_STA_ID=1, IWM_HOT_SPOT_CMD=0x53,
    IWM_CMD_WANT_RESP=1, IWM_CMD_FAILED_MSK=0x40, IWM_DEVICE_SYSTEM_TIME_REG=0xa0206c;
static uint64_t fakeNow = 10000000000ULL;
static void clock_get_uptime(uint64_t *out) { *out=fakeNow; }
static void absolutetime_to_nanoseconds(uint64_t in, uint64_t *out) { *out=in; }
struct IOTimerEventSource : OSObject {
    using Action=void(*)(OSObject*,IOTimerEventSource*);
    static unsigned live;static bool failCreate;
    OSObject *owner;Action action;unsigned scheduled=0,arms=0;bool cancelled=false;
    IOTimerEventSource(OSObject *o,Action a):owner(o),action(a){++live;}
    ~IOTimerEventSource(){--live;}
    static IOTimerEventSource *timerEventSource(OSObject *o,Action a){return failCreate?nullptr:new IOTimerEventSource(o,a);}
    void release(){assert(references);if(!--references)delete this;}
    void setTimeoutMS(unsigned ms){++arms;scheduled=ms;cancelled=false;}
    void cancelTimeout(){scheduled=0;cancelled=true;}
};
unsigned IOTimerEventSource::live=0;bool IOTimerEventSource::failCreate=false;
struct FakeLoop : OSObject {
    bool gated=true,failAdd=false;IOTimerEventSource *source=nullptr;
    bool inGate() const {return gated;}
    IOReturn addEventSource(IOTimerEventSource *t){if(failAdd)return -1;assert(!source);source=t;t->retain();return 0;}
    void removeEventSource(IOTimerEventSource *t){assert(source==t);source=nullptr;t->release();}
};
struct FakeGate : OSObject {
    FakeLoop *loop;
    explicit FakeGate(FakeLoop *l) : loop(l) {}
    IOReturn runAction(IOReturn (*fn)(OSObject*,void*,void*,void*,void*), void *arg) {
        bool old=loop->gated; loop->gated=true;
        auto result=fn(nullptr,arg,nullptr,nullptr,nullptr); loop->gated=old; return result;
    }
};
struct ItlAwdlProbeCounters {
    enum Counter { RocStarts,RocEnds,RocFailedNotifications,RocHardwareResets,RocIgnoredNotifications,
        RocCancels,RocCommandErrors,RocTimeouts,RocRequests,RocResponsesOk,RocRejected,Count };
    enum RocField { RocChannel, RocPhase, RocUid, RocRequestedGp2, RocStartGp2, RocEndGp2,
                    RocDurationTu, RocMaxDelayTu, RocStatus, RocSkipReason, RocResponseStatus,
                    TxEnabled, TxPhase, TxStatus, TxQueue, TxIndex, TxSubmittedGp2, PeerRocEnabled, RocTargetBoundaryGp2, RocTargetPeriodUs,
                    RocPlanNowGp2, RocObservationAgeUs, RocTargetSlot, RocTimingStreak, RocTimingResidualRaw,
                    RocStartBeforeBoundary, RocStartDistanceUs, RocWindowOverlapUs, RocFieldCount };
    ItlAwdl::RocHistory history;
    void appendRocHistory(const ItlAwdl::RocHistory::Row &row) { history.append(row); }
    bool planReady=true;
    bool planPeerRoc(uint32_t,uint32_t gp2,uint64_t,const uint8_t*,ItlAwdl::PeerRocPlan &p,uint32_t &reason) {
        reason=10;if(!planReady)return false;
        p={};p.boundaryGp2=gp2+8000;p.waitUs=8000;p.periodUs=65536;p.channel=149;p.slot=10;p.streak=3;p.peerId=7;p.epoch=2;reason=0;return true;
    }
    unsigned values[Count]={}, fields[RocFieldCount]={};
    void add(Counter c) { ++values[c]; }
    void setRoc(RocField f,uint32_t v) { fields[f]=v; }
};
struct FakeNode { unsigned ni_dtimperiod=1, ni_intval=100;int ni_chan=165; };
struct FakeChannel { unsigned ic_freq=2412,ic_flags=IEEE80211_CHAN_2GHZ; };
struct FakeIc {
    unsigned ic_state=IEEE80211_S_RUN,ic_opmode=IEEE80211_M_STA;
    FakeNode node; FakeNode *ic_bss=&node; FakeChannel ic_channels[256];
    uint8_t ic_myaddr[6]={2,1,2,3,4,5};
};
static unsigned ieee80211_chan2ieee(FakeIc*,int channel){return unsigned(channel);}
struct iwm_softc { FakeIc sc_ic; unsigned sc_flags=0; int sc_generation=1, ns_nstate=IEEE80211_S_RUN; uint8_t sc_enabled_capa[16]; };
struct iwm_rx_packet { struct { uint8_t flags=0; } hdr; uint8_t data[8]={}; };
struct iwm_host_cmd {
    unsigned id=0,flags=0; size_t resp_pkt_len=0; const void *data[2]={}; uint16_t len[2]={};
    iwm_rx_packet *resp_pkt=nullptr;
};
static unsigned iwm_rx_packet_payload_len(const iwm_rx_packet *) { return 8; }
class ItlIwm : public OSObject {
public:
    enum Mode { Normal, EarlyBoth, Reject, Timeout, Reset, LeaveRun, ResetTimer } mode=Normal;
    iwm_softc com; ItlAwdlProbeCounters counters; ItlAwdlProbeCounters *awdlProbeCounters=&counters;
    bool awdlProbeEnabled=true,awdlRocAttempted=false,awdlRocCancelSent=false;
    uint32_t awdlRocChannel=6,awdlRocTicks=0;
    uint32_t awdlMasterChannel=0;bool awdlEngineCycleAccounted=false;
    uint64_t awdlRocDeadlineNs=0;
    uint32_t awdlRocRecordedStarts=0,awdlRocRecordedEnds=0,awdlRocRecordedFailures=0;
    ItlAwdl::AuxRocSession awdlRocSession;
    bool awdlPeerRocEngine=false;
    bool awdlDiscoveryEnabled=false,awdlRocIsDiscovery=false;
    bool awdlRxControlEnabled=false,awdlRawRxEnabled=false;
    uint32_t awdlRxControlHomeChannel=0;ItlAwdl::ReceiveWindow awdlRawRxWindow;
    IOTimerEventSource *awdlDiscoveryTimer=nullptr;
    ItlAwdl::DiscoveryPolicy awdlDiscoveryPolicy;
    uint32_t awdlRocDurationTu=16,awdlRocHomeChannel=0;
    uint32_t awdlRocRxFrames=0,awdlRocAwdlFrames=0,awdlRocBeaconFrames=0,awdlRocOffHomeFrames=0;
    void awdlDiscoveryTimerStart();void awdlDiscoveryTimerStop();
    static void awdlDiscoveryTimerAction(OSObject*,IOTimerEventSource*);
    uint32_t awdlPeerRocAttempts=0,awdlPeerRocDelayTu=0,awdlPeerRocRequestedGp2=0,awdlPeerRocResponseStatus=UINT32_MAX;
    ItlAwdl::AuxRocCommand awdlPeerRocCommand={};
    void awdlPeerRocRecord(uint32_t,const uint8_t *notification=nullptr,size_t length=0);
    bool awdlPeerRocEnabled=false;unsigned awdlScanSubmitting=0;
    ItlAwdl::PeerRocPlan awdlPeerRocPlan;
    ItlAwdl::AuxTxOwnership awdlAuxTx;
    void awdlAuxTxPublish() {}
    void awdlAuxTxStart() {}
    void awdlAuxTxTick() {}
    bool awdlAuxTxDrain() { return !awdlAuxTx.pending; }
    FakeLoop loop; FakeGate gate{&loop};
    unsigned lastSubmittedChannel=0;
    unsigned sends=0,removes=0,frees=0; bool removeEnds=false; iwm_rx_packet packet;
    ItlIwm() { memset(com.sc_enabled_capa,255,sizeof(com.sc_enabled_capa)); }
    FakeLoop *getMainWorkLoop() { return &loop; }
    FakeGate *getMainCommandGate() { return &gate; }
    bool iwm_nic_lock(iwm_softc *) { return true; }
    void iwm_nic_unlock(iwm_softc *) {}
    unsigned clockReads=0;uint32_t finalClock=100000;
    uint32_t iwm_read_prph(iwm_softc *,unsigned) { return ++clockReads==1 ? 100000 : finalClock; }
    void event(unsigned action,unsigned status=1) {
        uint8_t bytes[24]={};
        ItlAwdl::rocWrite32(bytes,100000+(action==2?16384:0));
        ItlAwdl::rocWrite32(bytes+8,42); ItlAwdl::rocWrite32(bytes+12,4);
        ItlAwdl::rocWrite32(bytes+16,action); ItlAwdl::rocWrite32(bytes+20,status);
        awdlRocNotify(bytes,sizeof(bytes));
    }
    int iwm_send_cmd(iwm_softc *,iwm_host_cmd *cmd) {
        ++sends;lastSubmittedChannel=ItlAwdl::rocRead32(static_cast<const uint8_t*>(cmd->data[0])+16);
        assert(loop.inGate() && cmd->id==0x53 && cmd->len[0]==44);
        if(mode==Timeout) return EWOULDBLOCK;
        if(mode==Reset || mode==ResetTimer) { ++com.sc_generation; awdlRocQuiesced(); if(mode==ResetTimer)awdlDiscoveryTimerStop();return ENXIO; }
        ItlAwdl::rocWrite32(packet.data,42); ItlAwdl::rocWrite32(packet.data+4,mode==Reject?1:0);
        cmd->resp_pkt=&packet;
        if(mode==EarlyBoth) { event(1); event(2); }
        if(mode==LeaveRun) com.ns_nstate=0; // transition requested, actual state still RUN
        return 0;
    }
    int iwm_send_cmd_pdu(iwm_softc *,unsigned command,unsigned,size_t length,const void *data) {
        ++removes; const auto bytes=static_cast<const uint8_t*>(data);
        assert(command==0x53 && length==44 && ItlAwdl::rocRead32(bytes+4)==3 && ItlAwdl::rocRead32(bytes+8)==42);
        if(removeEnds) event(2);
        return 0;
    }
    void iwm_free_resp(iwm_softc *,iwm_host_cmd *cmd) { assert(cmd->resp_pkt==&packet); ++frees; cmd->resp_pkt=nullptr; }
    IOReturn setAwdlMasterChannel(uint32_t);void awdlEngineAccountCycle();
    void awdlRocTick(); void awdlRocPublish(); void awdlRocNotify(const uint8_t*,size_t);
    void awdlRocCancel(); void awdlRocQuiesced();
    void warmup() { for(unsigned i=0;i<10;++i) awdlRocTick(); }
};
#include "../../itlwm/awdl/AuxRocRuntime.hpp"
int main() {
    using P=ItlAwdl::RocPhase; using C=ItlAwdlProbeCounters;
    ItlIwm waiting;waiting.awdlPeerRocEnabled=true;waiting.counters.planReady=false;waiting.warmup();
    assert(!waiting.sends&&!waiting.awdlRocAttempted&&waiting.counters.fields[C::RocSkipReason]==10);
    waiting.counters.planReady=true;waiting.awdlRocTick();assert(waiting.sends==1&&waiting.awdlRocChannel==149);
    assert(waiting.counters.fields[C::RocTargetBoundaryGp2]==108000&&waiting.counters.fields[C::RocMaxDelayTu]==8);
    waiting.event(1);waiting.event(2);
    assert(waiting.counters.fields[C::RocStartBeforeBoundary]==1&&waiting.counters.fields[C::RocStartDistanceUs]==8000);
    assert(waiting.counters.fields[C::RocWindowOverlapUs]==8384);
    ItlIwm late;late.awdlPeerRocEnabled=true;late.finalClock=109000;late.warmup();
    assert(!late.sends&&!late.awdlRocAttempted&&late.counters.fields[C::RocSkipReason]==14);
    ItlIwm submitting;submitting.awdlScanSubmitting=1;submitting.warmup();assert(!submitting.sends);
    ItlIwm normal; normal.loop.gated=false; normal.warmup();
    assert(normal.sends==1 && normal.awdlRocSession.phase==P::AwaitStart && normal.frees==1);
    normal.event(1); normal.event(2);
    assert(normal.awdlRocSession.phase==P::Idle && normal.counters.values[C::RocStarts]==1 && normal.counters.values[C::RocEnds]==1);
    assert(normal.counters.fields[C::RocStartGp2]==100000 && normal.counters.fields[C::RocEndGp2]==116384);
    normal.warmup(); assert(normal.sends==1);
    ItlIwm early; early.mode=ItlIwm::EarlyBoth; early.warmup();
    assert(early.awdlRocSession.phase==P::Idle && early.counters.values[C::RocStarts]==1 && early.counters.values[C::RocEnds]==1);
    ItlIwm timeout; timeout.mode=ItlIwm::Timeout; timeout.warmup(); timeout.warmup();
    assert(timeout.sends==1 && timeout.awdlRocSession.phase==P::Quarantined);
    ItlIwm reset; reset.mode=ItlIwm::Reset; reset.warmup(); reset.warmup();
    assert(reset.sends==1 && reset.awdlRocSession.phase==P::Idle);
    ItlIwm reject; reject.mode=ItlIwm::Reject; reject.warmup(); reject.warmup();
    assert(reject.sends==1 && reject.counters.values[C::RocRejected]==1);
    assert(reject.counters.fields[C::RocResponseStatus]==1);
    ItlIwm leave; leave.mode=ItlIwm::LeaveRun; leave.warmup();
    assert(leave.sends==1 && leave.removes==1 && leave.awdlRocSession.phase==P::Stopping);
    ItlIwm cancel; cancel.warmup(); fakeNow+=3000000000ULL; cancel.awdlRocTick();
    assert(cancel.removes==1 && cancel.awdlRocSession.phase==P::Stopping);
    fakeNow+=3000000000ULL; cancel.awdlRocTick(); cancel.awdlRocTick();
    assert(cancel.removes==1 && cancel.awdlRocSession.phase==P::Quarantined);
    ItlIwm cancelEnd; cancelEnd.removeEnds=true; cancelEnd.warmup();
    fakeNow+=3000000000ULL; cancelEnd.awdlRocTick();
    assert(cancelEnd.removes==1 && cancelEnd.awdlRocSession.phase==P::Idle);
    ItlIwm busy; busy.com.sc_flags=IWM_FLAG_SCANNING; busy.warmup(); assert(!busy.sends);
    busy.com.sc_flags=0; busy.awdlRocTick(); assert(busy.sends==1);
    ItlIwm noDtim; noDtim.com.sc_ic.node.ni_dtimperiod=0; noDtim.warmup(); assert(!noDtim.sends);
    ItlIwm dfs; dfs.com.sc_ic.ic_channels[6].ic_flags|=IEEE80211_CHAN_DFS; dfs.warmup(); assert(!dfs.sends && dfs.awdlRocAttempted);
    ItlIwm disabled; disabled.awdlRocChannel=0; disabled.warmup(); assert(!disabled.sends);
    assert(normal.references==1 && normal.gate.references==1 && normal.loop.references==1);
    assert(reset.references==1 && reset.gate.references==1 && reset.loop.references==1);
    assert(cancel.references==1 && cancel.gate.references==1 && cancel.loop.references==1);
    ItlIwm pending; pending.warmup(); pending.event(1);
    assert(pending.awdlAuxTx.attempt() && pending.awdlAuxTx.submit(1,1,0,100));
    pending.event(2);
    assert(pending.awdlRocSession.phase==P::Draining && pending.awdlAuxTx.pending);
    // Exercise the actual bounded engine and HAL configuration implementation.
    // Cold firmware initialization stops the device before any ROC admission.
    // Preserve the unstarted boot request across those stops, then repeat dwells.
    ItlAwdl::gEngine={};ItlAwdl::gEngine.modeEnabled=1;ItlAwdl::gEngine.channel=6;
    ItlIwm cold;
    ++cold.com.sc_generation;cold.awdlRocQuiesced();
    ++cold.com.sc_generation;cold.awdlRocQuiesced();
    assert(ItlAwdl::gEngine.modeEnabled==1&&ItlAwdl::gEngine.deadlineNs==0);
    assert(cold.setAwdlMasterChannel(6)==kIOReturnNotReady);
    cold.warmup();assert(cold.sends==1);cold.event(1);cold.event(2);
    cold.awdlRocTick();cold.awdlRocTick();assert(cold.sends==2);
    cold.event(1);cold.event(2);cold.awdlRocTick();
    assert(ItlAwdl::gEngine.cyclesCompleted==2&&ItlAwdl::gEngine.endsTotal==2);
    assert(cold.counters.values[ItlAwdlProbeCounters::RocStarts]==2);
    assert(cold.counters.values[ItlAwdlProbeCounters::RocEnds]==2);
    ItlAwdl::gEngine={};ItlAwdl::gEngine.modeEnabled=1;ItlAwdl::gEngine.channel=6;
    ItlIwm engine;engine.loop.gated=false;
    assert(engine.setAwdlMasterChannel(6)==kIOReturnNotReady);
    engine.warmup();assert(engine.sends==1);engine.event(1);engine.event(2);
    engine.awdlRocTick();assert(ItlAwdl::gEngine.startsTotal==1&&ItlAwdl::gEngine.endsTotal==1);
    assert(engine.setAwdlMasterChannel(6)==0&&engine.awdlMasterChannel==6);
    assert(engine.setAwdlMasterChannel(44)==kIOReturnUnsupported);
    assert(engine.setAwdlMasterChannel(0)==kIOReturnBadArgument);
    engine.awdlRocTick();assert(engine.sends==2&&engine.lastSubmittedChannel==6);
    engine.event(1);engine.event(2);engine.awdlRocTick();
    assert(ItlAwdl::gEngine.startsTotal==2&&ItlAwdl::gEngine.endsTotal==2&&ItlAwdl::gEngine.cyclesCompleted==2);
    auto savedGeneration=engine.com.sc_generation; ++engine.com.sc_generation;
    assert(engine.setAwdlMasterChannel(6)==kIOReturnNotReady);engine.com.sc_generation=savedGeneration;
    engine.com.sc_flags=IWM_FLAG_RFKILL;assert(engine.setAwdlMasterChannel(6)==kIOReturnNotReady);engine.com.sc_flags=0;
    fakeNow=uint64_t(ItlAwdl::gEngine.deadlineNs)+1;
    assert(engine.setAwdlMasterChannel(6)==kIOReturnNotReady);
    unsigned before=engine.sends;engine.awdlRocTick();engine.awdlRocTick();
    assert(engine.sends==before&&ItlAwdl::gEngine.endsTotal==2&&ItlAwdl::gEngine.running==0&&engine.awdlMasterChannel==0);
    // Expiry must still service an outstanding cancellation and its timeout.
    ItlAwdl::gEngine={};ItlAwdl::gEngine.modeEnabled=1;ItlAwdl::gEngine.channel=6;
    ItlIwm expiring;expiring.warmup();expiring.event(1);
    fakeNow=uint64_t(ItlAwdl::gEngine.deadlineNs)+1;expiring.awdlRocTick();
    assert(expiring.removes==1&&expiring.awdlRocSession.phase==P::Stopping);
    fakeNow+=3000000000ULL;expiring.awdlRocTick();expiring.awdlRocTick();
    assert(expiring.awdlRocSession.phase==P::Quarantined&&ItlAwdl::gEngine.running==0&&ItlAwdl::gEngine.modeEnabled==0);
    ItlAwdl::gEngine={};ItlAwdl::gEngine.modeEnabled=1;ItlAwdl::gEngine.channel=6;
    ItlIwm quiesced;quiesced.warmup();quiesced.awdlMasterChannel=6;++quiesced.com.sc_generation;quiesced.awdlRocQuiesced();
    assert(quiesced.awdlMasterChannel==0&&ItlAwdl::gEngine.modeEnabled==0);
    quiesced.awdlRocTick();assert(quiesced.sends==1);
    // Even an armed window with no submitted command cannot restart on reset.
    ItlAwdl::gEngine={};ItlAwdl::gEngine.modeEnabled=1;ItlAwdl::gEngine.channel=6;
    ItlIwm armed;armed.com.sc_ic.ic_state=0;armed.awdlRocTick();
    assert(ItlAwdl::gEngine.deadlineNs!=0&&armed.sends==0);
    ++armed.com.sc_generation;armed.awdlRocQuiesced();
    assert(ItlAwdl::gEngine.modeEnabled==0&&ItlAwdl::gEngine.running==0);
    // Peer-only experiment: no plan means no fallback to the boot channel.
    ItlAwdl::gEngine={};ItlAwdl::gEngine.modeEnabled=1;ItlAwdl::gEngine.channel=6;
    ItlIwm peerLoop;peerLoop.awdlPeerRocEngine=peerLoop.awdlPeerRocEnabled=true;
    peerLoop.counters.planReady=false;peerLoop.warmup();assert(peerLoop.sends==0);
    peerLoop.counters.planReady=true;peerLoop.awdlMasterChannel=6;
    using H=ItlAwdl::RocHistory;H::Row row;
    for(unsigned attempt=1;attempt<=24;++attempt) {
        peerLoop.awdlRocTick();assert(peerLoop.sends==attempt&&peerLoop.lastSubmittedChannel==149);
        assert(peerLoop.awdlPeerRocDelayTu==(attempt&1u?8:300));
        assert(peerLoop.setAwdlMasterChannel(6)==kIOReturnUnsupported);
        assert(peerLoop.counters.history.read(peerLoop.counters.history.cursor-2,row));
        assert(row.v[H::Kind]==1&&row.v[H::PeerId]==7&&row.v[H::Epoch]==2);
        assert(row.v[H::Command4]==149&&row.v[H::Command9]==peerLoop.awdlPeerRocDelayTu);
        assert(row.v[H::Command6]==0&&(row.v[H::Command7]&0xffff)==0); // local MAC redacted
        peerLoop.event(1);peerLoop.event(2);peerLoop.awdlRocTick();
        assert(ItlAwdl::gEngine.cyclesCompleted==int(attempt));
        if(attempt==1) {
            peerLoop.counters.planReady=false;
            peerLoop.awdlRocTick();peerLoop.awdlRocTick();
            assert(peerLoop.sends==1); // lost/stale plan cannot authorize another ROC
            peerLoop.counters.planReady=true; // reacquired evidence allows the next attempt
        }
    }
    assert(peerLoop.counters.values[ItlAwdlProbeCounters::RocStarts]==24);
    assert(peerLoop.counters.values[ItlAwdlProbeCounters::RocEnds]==24);
    for(unsigned tick=0;tick<10;++tick)peerLoop.awdlRocTick();
    assert(peerLoop.sends==24&&ItlAwdl::gEngine.running==0);
    assert(peerLoop.counters.history.serial==96); // command, response, START, END per attempt
    assert(peerLoop.counters.history.read(95,row)&&row.v[H::Kind]==3&&row.v[H::NotificationLength]==24);
    assert(row.v[H::Notification4]==2&&row.v[H::Notification5]==1&&row.v[H::OverlapUs]>0);
    // No END before deadline: cancellation must drain or quarantine; never rearm.
    ItlAwdl::gEngine={};ItlAwdl::gEngine.modeEnabled=1;ItlAwdl::gEngine.channel=6;
    ItlIwm peerExpiry;peerExpiry.awdlPeerRocEngine=peerExpiry.awdlPeerRocEnabled=true;
    peerExpiry.warmup();peerExpiry.event(1);
    fakeNow=uint64_t(ItlAwdl::gEngine.deadlineNs)+1;peerExpiry.awdlRocTick();
    assert(peerExpiry.removes==1);fakeNow+=3000000000ULL;
    peerExpiry.awdlRocTick();peerExpiry.awdlRocTick();
    assert(peerExpiry.sends==1&&peerExpiry.awdlRocSession.phase==P::Quarantined);
    // A reset while peerWaiting for the first fresh peer plan cannot fall back to
    // the legacy one-shot path after it has disabled the bounded engine.
    ItlAwdl::gEngine={};ItlAwdl::gEngine.modeEnabled=1;ItlAwdl::gEngine.channel=6;
    ItlIwm peerWaiting;peerWaiting.awdlPeerRocEngine=peerWaiting.awdlPeerRocEnabled=true;
    peerWaiting.counters.planReady=false;peerWaiting.warmup();assert(peerWaiting.sends==0);
    ++peerWaiting.com.sc_generation;peerWaiting.awdlRocQuiesced();peerWaiting.counters.planReady=true;
    peerWaiting.warmup();assert(peerWaiting.sends==0&&ItlAwdl::gEngine.modeEnabled==0);
    // Actual discovery timer allocation/add failure is contained to the experiment.
    ItlIwm allocation;allocation.awdlDiscoveryEnabled=true;IOTimerEventSource::failCreate=true;
    allocation.awdlDiscoveryTimerStart();assert(!allocation.awdlDiscoveryEnabled&&allocation.awdlRocChannel==0);
    IOTimerEventSource::failCreate=false;
    ItlIwm addFailure;addFailure.awdlDiscoveryEnabled=true;addFailure.loop.failAdd=true;
    addFailure.awdlDiscoveryTimerStart();assert(!addFailure.awdlDiscoveryEnabled&&IOTimerEventSource::live==0);
    ItlAwdl::gEngine={};ItlAwdl::gEngine.modeEnabled=1;ItlAwdl::gEngine.channel=6;
    ItlIwm discovery;discovery.awdlPeerRocEngine=discovery.awdlPeerRocEnabled=discovery.awdlDiscoveryEnabled=true;
    discovery.counters.planReady=false;discovery.awdlDiscoveryTimerStart();auto timer=discovery.awdlDiscoveryTimer;
    ItlIwm::awdlDiscoveryTimerAction(&discovery,timer);
    assert(discovery.sends==0&&ItlAwdl::gEngine.deadlineNs==0&&timer->scheduled==500);
    discovery.warmup();assert(discovery.sends==1&&discovery.awdlRocIsDiscovery&&discovery.awdlRocDurationTu==64);
    discovery.event(1);discovery.event(2);
    for(unsigned attempt=1;attempt<4;++attempt) {
        ItlIwm::awdlDiscoveryTimerAction(&discovery,timer); // account previous drained session
        ItlIwm::awdlDiscoveryTimerAction(&discovery,timer); // submit next receive
        assert(discovery.sends==attempt+1&&discovery.lastSubmittedChannel==6);
        discovery.event(1);discovery.event(2);
    }
    ItlIwm::awdlDiscoveryTimerAction(&discovery,timer);
    for(unsigned i=0;i<8;++i)ItlIwm::awdlDiscoveryTimerAction(&discovery,timer);
    assert(discovery.sends==4); // sixty-second cooldown protects AP airtime
    fakeNow+=60000000001ULL;ItlIwm::awdlDiscoveryTimerAction(&discovery,timer);
    assert(discovery.sends==5&&discovery.lastSubmittedChannel==149);
    discovery.event(1);discovery.event(2);ItlIwm::awdlDiscoveryTimerAction(&discovery,timer);
    discovery.counters.planReady=true;ItlIwm::awdlDiscoveryTimerAction(&discovery,timer);
    assert(discovery.sends==6&&!discovery.awdlRocIsDiscovery&&discovery.awdlRocDurationTu==16);
    discovery.event(1);discovery.event(2);ItlIwm::awdlDiscoveryTimerAction(&discovery,timer);
    discovery.awdlDiscoveryTimerStop();discovery.awdlDiscoveryTimerStop();
    assert(!discovery.loop.source&&IOTimerEventSource::live==0&&discovery.references==1);
    ItlAwdl::gEngine={};ItlAwdl::gEngine.modeEnabled=1;ItlAwdl::gEngine.channel=6;
    ItlIwm detachTimer;detachTimer.awdlDiscoveryEnabled=detachTimer.awdlPeerRocEnabled=detachTimer.awdlPeerRocEngine=true;
    detachTimer.awdlRocTicks=10;detachTimer.counters.planReady=false;detachTimer.mode=ItlIwm::ResetTimer;
    detachTimer.awdlDiscoveryTimerStart();timer=detachTimer.awdlDiscoveryTimer;
    ItlIwm::awdlDiscoveryTimerAction(&detachTimer,timer);
    assert(detachTimer.sends==1&&!detachTimer.awdlDiscoveryTimer&&IOTimerEventSource::live==0);
    assert(detachTimer.references==1&&detachTimer.gate.references==1&&detachTimer.loop.references==1);
    // No timer rearm after an exhausted budget or a normally drained deadline.
    ItlAwdl::gEngine={};ItlAwdl::gEngine.modeEnabled=1;ItlAwdl::gEngine.channel=6;
    ItlIwm timerDone;timerDone.awdlDiscoveryEnabled=timerDone.awdlPeerRocEnabled=timerDone.awdlPeerRocEngine=true;
    timerDone.awdlDiscoveryPolicy.aligned=24;timerDone.awdlRocTicks=10;timerDone.awdlDiscoveryTimerStart();
    timer=timerDone.awdlDiscoveryTimer;auto arms=timer->arms;
    ItlIwm::awdlDiscoveryTimerAction(&timerDone,timer);assert(timer->arms==arms&&timerDone.sends==0);
    timerDone.awdlDiscoveryTimerStop();assert(IOTimerEventSource::live==0);
    ItlAwdl::gEngine={};ItlAwdl::gEngine.modeEnabled=1;ItlAwdl::gEngine.channel=6;
    ItlIwm timerExpiry;timerExpiry.awdlDiscoveryEnabled=timerExpiry.awdlPeerRocEnabled=timerExpiry.awdlPeerRocEngine=true;
    timerExpiry.counters.planReady=false;timerExpiry.warmup();timerExpiry.event(1);timerExpiry.removeEnds=true;
    timerExpiry.awdlDiscoveryTimerStart();timer=timerExpiry.awdlDiscoveryTimer;arms=timer->arms;
    fakeNow=uint64_t(ItlAwdl::gEngine.deadlineNs)+1;ItlIwm::awdlDiscoveryTimerAction(&timerExpiry,timer);
    assert(timerExpiry.removes==1&&timerExpiry.awdlRocSession.phase==P::Idle&&timer->arms==arms);
    timerExpiry.awdlDiscoveryTimerStop();assert(IOTimerEventSource::live==0);
    // Probe60: control uses real associated home vs social6, never peer model.
    ItlAwdl::gEngine={};ItlAwdl::gEngine.modeEnabled=1;ItlAwdl::gEngine.channel=6;
    ItlIwm control;control.awdlDiscoveryEnabled=control.awdlPeerRocEnabled=control.awdlPeerRocEngine=true;
    control.awdlRxControlEnabled=control.awdlRawRxEnabled=true;control.awdlRocTicks=10;
    control.com.sc_ic.node.ni_chan=36;control.com.sc_ic.ic_channels[36].ic_flags=IEEE80211_CHAN_5GHZ;
    control.counters.planReady=true; // a model must NOT hijack this RX control
    for(unsigned i=0;i<24;++i) {
        if(i && !(i%4))fakeNow+=60000000001ULL;
        control.awdlRocTick();
        assert(control.sends==i+1 && control.lastSubmittedChannel==((i/4)%2?6:36));
        assert(control.awdlRocIsDiscovery && control.awdlRocDurationTu==64);
        control.event(1);assert(control.awdlRawRxWindow.valid && !control.awdlRawRxWindow.ended);
        control.event(2);assert(control.awdlRocSession.phase==P::Idle);
        assert(control.awdlRawRxWindow.ended && control.awdlRawRxWindow.contains(1,control.lastSubmittedChannel,100001));
        control.awdlRocTick(); // release the previous drained session
    }
    fakeNow+=60000000001ULL;for(unsigned i=0;i<10;++i)control.awdlRocTick();assert(control.sends==24);
    control.awdlRocQuiesced();assert(!control.awdlRawRxWindow.valid && !ItlAwdl::gEngine.modeEnabled);
    ItlAwdl::gEngine={};ItlAwdl::gEngine.modeEnabled=1;
    ItlIwm changed;changed.awdlDiscoveryEnabled=changed.awdlPeerRocEnabled=changed.awdlPeerRocEngine=changed.awdlRxControlEnabled=true;
    changed.awdlRocTicks=10;changed.com.sc_ic.node.ni_chan=36;
    changed.awdlRocTick();changed.event(1);changed.event(2);changed.awdlRocTick();
    changed.com.sc_ic.node.ni_chan=40;changed.awdlRocTick();
    assert(changed.sends==1 && !ItlAwdl::gEngine.modeEnabled);
    puts("actual ROC runtime bridge: replies, early events, cancellation, reset, admission and one-shot gates PASS");
}
