// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
namespace ItlIbss {
// Software receive-window timing probe on an already resident channel 6.
// No RF permission. Default diagnostic cap is three; a live bounded native lease may renew up to256.
struct WindowPlan {
    uint32_t generation=0,peer=0,epoch=0,boundary=0,period=0,streak=0;
    uint32_t observedGp2=0,open=0,close=0;uint64_t observedHost=0;
    // Frozen actual selection input, diagnostics only; never RF authority.
    uint32_t awPeriod=0,presence=0,downCounter=0,boundarySequence=0,selectedSlot=0,candidateOffset=0;
    uint32_t sequenceCount=0,sequenceSteps=0,sequenceFill=0,sequenceEncoding=0,sequenceDuplicates=0;
    uint16_t sequenceEntries[16]={};
    uint32_t rawAwSequence=0,txDelayUs=0;
    uint32_t version=1; // v1 original8TU, v2 EAW interior with2TU boundary guards.
};
inline int64_t windowDelta(uint32_t a,uint32_t b) {const auto d=a-b;return d<0x80000000u?int64_t(d):int64_t(d)-0x100000000ll;}
struct WindowProbe {
    enum Phase:uint32_t { Idle=0,Armed=1,Open=2,Closed=3,Cancelled=4 };
    Phase phase=Idle;WindowPlan plan={};uint64_t wakeNs=0;
    uint32_t actualOpen=0,actualClose=0,rawFrames=0,peerFrames=0,cancelReason=0;bool invalidated=false;
    uint32_t eawAFRecords=0;bool eawAFOverflow=false;
    unsigned attempts=0;
    bool lateCloseCancelled(bool continuous=false)const {
        return phase==Cancelled && cancelReason==2 && (actualOpen||continuous) && !invalidated &&
            plan.version==2 && attempts<(continuous?256u:3u);
    }
    bool canPlan(bool replanLateClose=false,bool continuous=false)const {
        return phase==Idle || (phase==Closed && attempts<(continuous?256u:3u)) || (replanLateClose && lateCloseCancelled(continuous));
    }
    // Supplemental timestamp evidence across the frozen EAW, independent of
    // the original Open-phase8TU counting window. Never authorizes RF work.
    unsigned claimEawAf(uint32_t gen,uint32_t peer,uint32_t epoch,uint32_t gp2,bool consistent) {
        if(phase==Idle || phase==Cancelled || invalidated || !consistent || !peer ||
           gen!=plan.generation || epoch!=plan.epoch || !plan.period ||
           windowDelta(gp2,plan.boundary)<0 || windowDelta(gp2,plan.boundary)>=plan.period ||
           uint32_t(gp2-plan.observedGp2)>500000)return 0;
        if(eawAFRecords<16){++eawAFRecords;return 1;}
        if(!eawAFOverflow){eawAFOverflow=true;return 2;}
        return 0;
    }
    bool arm(const WindowPlan &p,uint32_t gen,uint32_t now,uint64_t host,uint64_t leaseEnd,bool replanLateClose=false,bool continuous=false) {
        const auto lead=windowDelta(p.open,now),duration=windowDelta(p.close,p.open);
        const bool wide=p.version==2;
        const bool shape=wide ? p.period>=16384 && p.open-p.boundary==2048 && duration==int64_t(p.period)-4096 : p.version==1 && p.open-p.boundary==4096 && duration==8192;
        if(!canPlan(replanLateClose,continuous) || (continuous&&phase!=Idle&&p.peer!=plan.peer) || (phase==Cancelled && (p.peer!=plan.peer || windowDelta(p.observedGp2,plan.close)<=0)) ||
           (phase!=Idle && (!wide || p.generation!=plan.generation || p.epoch!=plan.epoch || windowDelta(p.boundary,plan.boundary)<=0)) || !gen || gen!=p.generation || !p.peer || !p.epoch || p.streak<3 ||
           p.period<16384 || p.period>=0x80000000u || !shape || lead<20000 || lead>250000 ||
           uint64_t(now-p.observedGp2)+uint64_t(lead+duration)>500000 ||
           p.observedHost>host || host-p.observedHost>500000000ull ||
           host>UINT64_MAX-uint64_t(lead+duration+5000)*1000 ||
           host+uint64_t(lead+duration+5000)*1000>=leaseEnd)return false;
        plan=p;phase=Armed;++attempts;actualOpen=actualClose=rawFrames=peerFrames=cancelReason=0;invalidated=false;
        eawAFRecords=0;eawAFOverflow=false;wakeNs=host+uint64_t(lead)*1000;return true;
    }
    bool pending()const{return phase==Armed || phase==Open;}
    void cancel(uint32_t reason){if(pending()){phase=Cancelled;cancelReason=reason;wakeNs=0;}}
    // Return transition code: 0 pending/idle, 2 opened, 3 closed, 4 cancelled.
    unsigned sample(uint32_t gen,uint32_t gp2,uint64_t host,uint64_t leaseEnd,bool healthy) {
        if(!pending())return 0;
        if(!healthy || invalidated || gen!=plan.generation || host>=leaseEnd){cancel(1);return 4;}
        const uint32_t target=phase==Armed?plan.open:plan.close;const auto late=windowDelta(gp2,target);
        if(late<0){wakeNs=host+uint64_t(-late)*1000;return 0;}
        if(late>2048){cancel(2);return 4;}
        if(phase==Armed){actualOpen=gp2;phase=Open;wakeNs=host+uint64_t(plan.close-gp2)*1000;return 2;}
        actualClose=gp2;phase=Closed;wakeNs=0;return 3;
    }
    bool inside(uint32_t gen,uint32_t gp2)const {
        return !invalidated && phase==Open && gen==plan.generation && windowDelta(gp2,plan.open)>=0 && windowDelta(gp2,plan.close)<0;
    }
    void raw(uint32_t gen,uint32_t gp2){if(inside(gen,gp2))++rawFrames;}
    void peer(uint32_t gen,uint32_t id,uint32_t epoch,uint32_t gp2) {
        if(id==plan.peer && epoch==plan.epoch && inside(gen,gp2))++peerFrames;
    }
};
} // namespace ItlIbss
