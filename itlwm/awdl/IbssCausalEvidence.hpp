// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "ProtocolModel.hpp"
#include "IbssExperiment.hpp"
namespace ItlIbss {
// Numeric copy of the previous peer state only. No MAC addresses, allocation,
// timers or transport. Freeze BEFORE ingesting the AF under evaluation.
struct CausalPrior {
    ItlAwdl::SyncParameters sync={};
    ItlAwdl::ChannelSequence channels={};
    uint32_t generation=0,epoch=0,peer=0,received=0,delay=0,streak=0;
    bool valid=false;
};
struct CausalView {
    uint32_t boundary=0,boundarySequence=0,period=0,phase=0,age=0,slot=0,channel=0,entry=0;
};
inline bool freezeCausalPrior(const ItlAwdl::Peer &peer,uint32_t generation,uint32_t epoch,uint32_t rx,CausalPrior &out) {
    out={};
    if(!generation || !epoch || !peer.diagnosticId || !peer.hasSync || !peer.hasChannels ||
       !peer.timing.consistent || peer.timing.streak<3 || uint32_t(rx-peer.syncReceivedGp2)>500000 ||
       !ItlAwdl::scheduleConsistentWithSync(peer.sync,peer.channels))return false;
    // Deliberately omit address/master/election data.
    out.sync.awPeriod=peer.sync.awPeriod;out.sync.presenceMode=peer.sync.presenceMode;
    out.sync.nextAwSequence=peer.sync.nextAwSequence;out.sync.txDownCounter=peer.sync.txDownCounter;
    out.channels=peer.channels;out.generation=generation;out.epoch=epoch;out.peer=peer.diagnosticId;
    out.received=peer.syncReceivedGp2;out.delay=peer.timing.txDelayUs;out.streak=peer.timing.streak;
    out.valid=true;return true;
}
inline bool evaluateCausalPrior(const CausalPrior &p,uint32_t generation,uint32_t epoch,uint32_t peer,uint32_t rx,CausalView &out) {
    out={};
    if(!p.valid || !generation || p.generation!=generation || p.epoch!=epoch || !peer || p.peer!=peer)return false;
    ItlAwdl::WindowPrediction next={};
    if(!ItlAwdl::predictWindowWithTxDelay(p.sync,p.received,rx,500000,p.delay,next))return false;
    const uint32_t period=uint32_t(p.sync.awPeriod)*p.sync.presenceMode*1024;
    auto current=next;current.nextBoundaryGp2-=period;
    current.nextBoundarySequence=uint16_t(current.nextBoundarySequence-p.sync.presenceMode);
    ItlAwdl::SlotView slot={};if(!ItlAwdl::predictNextSlot(p.sync,p.channels,current,slot))return false;
    const uint32_t phase=rx-current.nextBoundaryGp2;
    if(phase>=period)return false;
    out.boundary=current.nextBoundaryGp2;out.boundarySequence=current.nextBoundarySequence;
    out.period=period;out.phase=phase;out.age=rx-p.received;out.slot=slot.slot;out.channel=slot.channel;
    out.entry=uint32_t(p.channels.entries[slot.slot].channel)|(uint32_t(p.channels.entries[slot.slot].auxiliary)<<8);
    return true;
}
struct CausalBudget {
    unsigned count=0;bool overflow=false;
    unsigned claim(){if(count<8)return ++count;if(!overflow){overflow=true;return 9;}return 0;}
};
inline ExperimentRows::Row causalEvidenceRow(const CausalPrior &p,const CausalView &v,
            uint32_t rx,uint32_t channel,uint64_t host,unsigned index,bool raw,bool currentConsistent) {
    using E=ExperimentRows;E::Row r;const auto us=host/1000;
    r.v[E::Kind]=raw?34:33;r.v[E::Code]=0x100|index;
    r.v[E::HostUsLow]=uint32_t(us);r.v[E::HostUsHigh]=uint32_t(us>>32);
    r.v[E::Generation]=p.generation;r.v[E::MacContext]=p.peer;r.v[E::Length]=p.epoch;r.v[E::Phase]=v.boundary;
    if(raw){
        r.v[E::Gp2]=p.received;r.v[E::Flags]=p.sync.nextAwSequence;r.v[E::Status]=p.sync.txDownCounter;
        r.v[E::Valid]=p.sync.awPeriod|(uint32_t(p.sync.presenceMode)<<16);r.v[E::Channel]=v.entry;
        r.v[E::FrameControl]=p.channels.count|(uint32_t(p.channels.steps)<<8)|(uint32_t(p.channels.encoding)<<16)|(uint32_t(p.channels.duplicates)<<24);
        r.v[E::Detail0]=p.delay;r.v[E::Detail1]=p.channels.fill;r.v[E::Result]=1;
    }else{
        r.v[E::Gp2]=rx;r.v[E::Flags]=p.streak;r.v[E::Status]=channel;r.v[E::Valid]=v.slot;
        r.v[E::Channel]=v.channel;r.v[E::FrameControl]=v.boundarySequence;
        r.v[E::Detail0]=v.phase;r.v[E::Detail1]=v.age;r.v[E::Result]=currentConsistent;
    }
    return r;
}
}
