// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "NativeReceivePolicy.hpp"
#include "ActionBuilder.hpp"
#include "NativeSyncProfile.hpp"
namespace ItlAwdl {
// Offline self-originated test network, not a claim to follow/join a real peer.
struct SelfControlInput {
    uint8_t source[6]={};uint16_t sequence=0,epochAwSequence=0;
    uint32_t generation=0,currentGeneration=0,epochGp2=0,nowGp2=0;
    uint64_t epochHostNs=0,nowHostNs=0,leaseEndNs=0;
    uint32_t selfMetric=60,advertisementCounter=1;
    uint32_t advertisementPeriod=NativeSyncProfile::advertisementPeriod;
    uint8_t subtype=0;bool receiveValidation=false;
};
struct SelfControlEvidence {
    uint32_t generation=0,epochGp2=0,targetGp2=0,elapsedUs=0;
    uint16_t currentAw=0,downCounterTu=0;
    uint32_t nextBoundaryGp2=0,quantizationUs=0;
};
// Covers one full 16-EAW channel table and the existing 2ms transport lifetime.
// No RF authority: caller still needs the real session/permission/ownership gate.
constexpr uint32_t kSelfControlPeriodUs=NativeSyncProfile::periodUs;
constexpr uint64_t kSelfControlHorizonNs=(uint64_t(16)*kSelfControlPeriodUs+2000)*1000;
inline bool buildSelfControlAdvertisement(const SelfControlInput &i,uint8_t *out,size_t capacity,
                                           size_t &used,SelfControlEvidence &evidence) {
    used=0;evidence={};
    if(!i.generation || i.generation!=i.currentGeneration || !i.epochHostNs ||
       i.nowHostNs<i.epochHostNs || i.nowHostNs>=i.leaseEndNs ||
       i.leaseEndNs-i.nowHostNs<kSelfControlHorizonNs ||
       i.leaseEndNs-i.epochHostNs>nativeReceiveLimit(i.receiveValidation) || i.epochAwSequence%4 ||
       !i.selfMetric || !i.advertisementCounter ||
       !NativeSyncProfile::supportsAdvertisementPeriod(i.advertisementPeriod))return false;
    const uint64_t hostAgeUs=(i.nowHostNs-i.epochHostNs)/1000;
    const uint32_t elapsed=i.nowGp2-i.epochGp2;
    if(hostAgeUs>nativeReceiveLimit(i.receiveValidation)/1000 || elapsed>nativeReceiveLimit(i.receiveValidation)/1000 ||
       (hostAgeUs>elapsed?hostAgeUs-elapsed:elapsed-hostAgeUs)>2048)return false;
    const uint32_t elapsedTu=elapsed/1024;
    ActionParameters p={};
    for(unsigned n=0;n<6;++n) {
        p.source[n]=p.sync.masterAddress[n]=p.election.masterAddress[n]=p.election.syncAddress[n]=i.source[n];
    }
    p.subtype=i.subtype;p.sequence=i.sequence;
    // GP2 is a proposed common target timestamp; actual firmware airtime remains unproven.
    p.phyTransmitTime=p.targetTransmitTime=i.nowGp2;
    auto &s=p.sync;s.nextChannel=s.masterChannel=6;s.awPeriod=NativeSyncProfile::awPeriod;s.presenceMode=NativeSyncProfile::presence;
    s.afPeriod=i.advertisementPeriod;s.flags=0x1800;s.awExtensionLength=NativeSyncProfile::awExtension;s.awCommonLength=NativeSyncProfile::awLength;
    s.minExtension=s.maxMulticastExtension=s.maxUnicastExtension=s.maxActionExtension=3;
    s.nextAwSequence=uint16_t(i.epochAwSequence+elapsedTu/NativeSyncProfile::awPeriod);
    s.apAlignment=s.nextAwSequence;s.txDownCounter=uint16_t(NativeSyncProfile::awPeriod*NativeSyncProfile::presence-elapsedTu%(NativeSyncProfile::awPeriod*NativeSyncProfile::presence));
    const uint16_t spent=NativeSyncProfile::awPeriod*NativeSyncProfile::presence-s.txDownCounter;
    s.remainingAwLength=spent<NativeSyncProfile::awLength?NativeSyncProfile::awLength-spent:0;
    p.channels.count=16;p.channels.steps=4;p.channels.encoding=3;p.channels.fill=0xffff;
    for(auto &c:p.channels.entries)c={6,81};
    p.election.v2=true;p.election.height=0;
    p.election.masterMetric=p.election.selfMetric=i.selfMetric;
    p.election.masterCounter=p.election.selfCounter=i.advertisementCounter;
    if(!buildControlAction(p,out,capacity,used))return false;
    evidence.generation=i.generation;evidence.epochGp2=i.epochGp2;evidence.targetGp2=i.nowGp2;
    evidence.elapsedUs=elapsed;evidence.currentAw=s.nextAwSequence;evidence.downCounterTu=s.txDownCounter;
    evidence.nextBoundaryGp2=i.epochGp2+uint32_t((elapsed/kSelfControlPeriodUs+1)*kSelfControlPeriodUs);
    evidence.quantizationUs=elapsed%1024;
    return true;
}
} // namespace ItlAwdl
