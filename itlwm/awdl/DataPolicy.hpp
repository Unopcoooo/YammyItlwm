// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "DataFrames.hpp"
#include "PeerTable.hpp"
#include "AuxRoc.hpp"
#include "IbssWindowProbe.hpp"
namespace ItlAwdl {
enum class DataRadioKind {Aux,ExclusiveIbss};
// Borrowed under the main gate. The HAL must obtain these facts from the live
// exclusive owner and peer port, not from a requested or predicted configuration.
struct DataIbssLease {
 uint32_t generation=0;uint64_t deadlineNs=0;
 uint8_t local[6]={},peer[6]={};
 bool ownsRadio=false,firmwareReady=false,peerReady=false,resetRequested=false;
 bool peerFollowValid=false;
 const ItlIbss::WindowProbe *window=nullptr;
};
// Borrowed model/session pointers are valid only inside the shared main gate.
struct DataContext {
    DataRadioKind radioKind=DataRadioKind::Aux;
    const DataIbssLease *ibss=nullptr;
    DataIbssLease ibssStorage;
    const DataIbssLease *ibssView()const{return ibss?ibss:&ibssStorage;}
    const PeerTable *peers=nullptr;const AuxRocSession *roc=nullptr;
    uint32_t generation=0,modelGeneration=0,modelEpoch=0,gp2=0,channel=0,durationUs=0,targetGp2=0;
    uint16_t aw=0;uint64_t nowNs=0,modelHostNs=0,deadlineNs=0;
    bool gateHeld=false,backendReady=false,associated=false,aligned=false;
};
inline bool dataIbssResident(const DataContext &c){
 const auto s=c.ibssView();
 return c.radioKind==DataRadioKind::ExclusiveIbss&&s&&s->ownsRadio&&s->firmwareReady&&
  s->peerReady&&!s->resetRequested&&s->peerFollowValid&&s->generation==c.generation&&
  dataUnicast(s->local)&&dataUnicast(s->peer)&&memcmp(s->local,s->peer,6)&&
  c.channel==6&&c.nowNs<s->deadlineNs&&c.deadlineNs<=s->deadlineNs;
}
inline bool dataRadioContextReady(const DataContext &c){
    return c.gateHeld&&(c.radioKind==DataRadioKind::Aux?c.associated:dataIbssResident(c))&&c.generation&&c.modelEpoch&&c.peers&&
        c.modelGeneration==c.generation&&c.peers->count<=kMaxPeers&&
        c.nowNs>=c.modelHostNs&&c.nowNs-c.modelHostNs<=500000000ULL&&c.deadlineNs>c.nowNs;
}
inline bool dataContextReady(const DataContext &c){return c.backendReady&&dataRadioContextReady(c);}
inline bool dataActualRadioWindow(const DataContext &c,bool tx){
    if(c.radioKind==DataRadioKind::ExclusiveIbss){
     if(!dataRadioContextReady(c))return false;
     // RX remains resident after a TX window closes, as proven by the reply trial.
     if(!tx)return true;
     const auto w=c.ibssView()->window;
     bool selected=false;
     if(w)for(unsigned i=0;i<c.peers->count;++i){const auto &p=c.peers->peers[i];
      if(p.diagnosticId&&p.diagnosticId==w->plan.peer&&!memcmp(p.address,c.ibssView()->peer,6))selected=true;
     }
     return selected&&w->plan.version==2&&w->plan.period>=16384&&w->plan.period<=65536&&
      w->plan.open-w->plan.boundary==2048&&w->plan.close-w->plan.boundary==w->plan.period-2048&&
      w->inside(c.generation,c.gp2)&&w->plan.epoch==c.modelEpoch&&
      c.gp2-w->plan.observedGp2<=500000&&w->plan.observedHost<=c.nowNs&&
      c.nowNs-w->plan.observedHost<=500000000ULL&&
      ItlIbss::windowDelta(w->plan.close,c.gp2)>=5000&&c.deadlineNs-c.nowNs>=5000000ULL;
    }
    if(!dataRadioContextReady(c)||!c.roc||!c.aligned||c.channel!=6||
       !c.roc->mayTransmit(c.generation)||!c.roc->startCount||c.roc->endCount||
       !c.durationUs||c.durationUs>65536||uint32_t(c.roc->startGp2-c.targetGp2)>2000)return false;
    const uint32_t age=c.gp2-c.roc->startGp2;
    if(age>=c.durationUs)return false;
    // Conservative 6 Mbps, one nonaggregated MTU frame incl. retry/ACK margin.
    // The real descriptor must enforce a bounded lifetime; this is not RF proof.
    return !tx||(c.durationUs-age>=5000&&c.deadlineNs-c.nowNs>=5000000ULL);
}
inline bool dataActualWindow(const DataContext &c,bool tx){return c.backendReady&&dataActualRadioWindow(c,tx);}
inline bool dataPeerAvailable(const Peer &p,const uint8_t local[6],const DataContext &c,bool multicast){
    if(c.radioKind==DataRadioKind::ExclusiveIbss){
     // First system adapter is one known unicast peer. NDP/multicast is a
     // separate capability; do not claim it from the unicast experiment.
     if(multicast||!dataIbssResident(c)||memcmp(local,c.ibssView()->local,6)||
        memcmp(p.address,c.ibssView()->peer,6)||!p.diagnosticId||!p.hasSync||!p.hasChannels||!p.hasElection||
        c.gp2-p.lastSeenGp2>500000||c.gp2-p.syncReceivedGp2>500000||
        !p.timing.consistent||p.timing.streak<3||p.timing.receivedGp2!=p.syncReceivedGp2)return false;
     return true;
    }
    if(!dataUnicast(p.address)||!memcmp(p.address,local,6)||!p.hasSync||!p.hasChannels||!p.hasElection||
       c.gp2-p.lastSeenGp2>500000||c.gp2-p.syncReceivedGp2>500000||
       !p.timing.consistent||p.timing.streak<3||p.timing.receivedGp2!=p.syncReceivedGp2||
       memcmp(p.sync.masterAddress,local,6)||memcmp(p.election.masterAddress,local,6))return false;
    // Only the currently implemented local-master clock and channel-6 subset.
    if(!p.sync.awPeriod||p.sync.awPeriod>64||!p.sync.presenceMode||p.sync.presenceMode>4||
       uint32_t(p.sync.awPeriod)*p.sync.presenceMode>64||
       (p.channels.encoding!=1&&p.channels.encoding!=3))return false;
    WindowPrediction w;SlotView slot;
    if(!predictWindowWithTxDelay(p.sync,p.syncReceivedGp2,c.gp2,500000,p.timing.txDelayUs,w))return false;
    const uint32_t period=uint32_t(p.sync.awPeriod)*p.sync.presenceMode*1024;
    const uint16_t current=uint16_t(w.nextBoundarySequence-p.sync.presenceMode);
    w.nextBoundarySequence=current;
    if(!predictNextSlot(p.sync,p.channels,w,slot)||slot.channel!=c.channel||current!=c.aw)return false;
    const uint8_t flags=p.channels.entries[slot.slot].auxiliary;
    if(flags!=(p.channels.encoding==1?0x2b:0x51))return false;
    const int64_t phase=signedGp2Delta(w.nextBoundaryGp2-period,c.targetGp2);
    if(phase < -2048||phase>2048||w.waitMicroseconds<5000)return false;
    return !multicast||slot.multicast;
}
inline bool dataDestinationAllowed(const uint8_t destination[6],const uint8_t local[6],const DataContext &c){
    if(!dataActualWindow(c,true)||!dataUnicast(local)||!destination)return false;
    const bool multicast=(destination[0]&1)!=0;
    if(multicast&&(destination[0]!=0x33||destination[1]!=0x33))return false;
    if(!multicast&&!dataUnicast(destination))return false;
    for(unsigned i=0;i<c.peers->count;++i){const auto &p=c.peers->peers[i];
        if((multicast||!memcmp(p.address,destination,6))&&dataPeerAvailable(p,local,c,multicast))return true;
    }
    return false;
}
}
