// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Independent C++ adaptation of Owly fixed-radio election/clock behaviour.
// Reference: t4t5/owly 9a676e11, election.rs, sync.rs, node.rs (MIT).
// No I/O, allocation or scheduling authority; the caller owns runtime gates.
#include "ProtocolModel.hpp"
#include "ActionBuilder.hpp"
namespace ItlAwdl {
struct FixedControlView {
 ElectionView election={};uint32_t generation=0,sourceAgeUs=0;
 uint16_t aw=0,down=0;uint8_t source[6]={};
};
class FixedChannelControl {
 struct Entry {Advertisement advert={};uint64_t hostNs=0;uint32_t gp2=0;bool mif=false,version=false;};
 Entry peers[kMaxPeers]={};unsigned count=0;uint32_t generation=0;uint64_t lastEventNs=0;uint8_t local[6]={};
 static bool address(const uint8_t *p){uint8_t any=0;for(unsigned i=0;i<6;++i)any|=p[i];return any&&!(p[0]&1);}
 static bool fresh(const Entry &p,uint64_t now){return now>=p.hostNs&&now-p.hostNs<=5000000000ULL;}
public:
 bool reset(const uint8_t self[6],uint32_t gen){
  count=0;generation=0;lastEventNs=0;if(!self||!gen||!address(self))return false;
  memcpy(local,self,6);generation=gen;return true;
 }
 bool observe(const uint8_t *wire,size_t length,uint32_t gen,uint32_t gp2,uint64_t hostNs){
  if(!generation||gen!=generation||!hostNs||hostNs<lastEventNs)return false;
  Advertisement a;if(decodeAdvertisement(wire,length,a)!=AdvertisementResult::Valid||
   peerSameAddress(a.sender,local)||!a.election.v2||!address(a.election.masterAddress)||
   !address(a.election.syncAddress)||a.sync.awPeriod!=16||a.sync.presenceMode!=4)return false;
  bool version=false,seen=false;
  size_t at=24+((wire[1]&0x80)?4:0)+16;
  while(at<length){const auto n=syncRead16(wire+at+1);
   if(wire[at]==21){if(seen||n!=2)return false;seen=true;version=wire[at+3]&&wire[at+4];}
   at+=n+3;
  }
  for(unsigned i=0;i<count;){if(!fresh(peers[i],hostNs)){peers[i]=peers[--count];}else ++i;}
  unsigned index=0;while(index<count&&!peerSameAddress(peers[index].advert.sender,a.sender))++index;
  if(index==count){if(count==kMaxPeers)return false;peers[count++]={};}
  auto &p=peers[index];if(p.hostNs&&hostNs<p.hostNs)return false;
  p.advert=a;p.hostNs=hostNs;p.gp2=gp2;p.mif|=wire[24+((wire[1]&0x80)?4:0)+6]==3;p.version|=version;lastEventNs=hostNs;return true;
 }
 bool view(uint32_t gen,uint32_t gp2,uint64_t hostNs,FixedControlView &out)const{
  out={};if(!generation||gen!=generation||!hostNs||hostNs<lastEventNs)return false;
  const Entry *best=nullptr;uint32_t metric=60,counter=0,height=0;const uint8_t *member=local;
  for(unsigned i=0;i<count;++i){const auto &p=peers[i];const auto &e=p.advert.election;
   if(!fresh(p,hostNs)||!p.mif||!p.version||e.height>=10||peerSameAddress(e.syncAddress,local))continue;
   if(e.masterMetric<metric||(e.masterMetric==metric&&e.masterCounter<counter))continue;
   if(e.masterMetric==metric&&e.masterCounter==counter&&
      (e.height>height||(e.height==height&&memcmp(p.advert.sender,member,6)<=0)))continue;
   best=&p;metric=e.masterMetric;counter=e.masterCounter;height=e.height;member=p.advert.sender;
  }
  // A bounded follower needs a live sync source, not a fabricated self epoch.
  if(!best)return false;
  const uint64_t hostAge=(hostNs-best->hostNs)/1000;const uint32_t age=gp2-best->gp2;
  if(hostAge>500000||age>500000||(hostAge>age?hostAge-age:age-hostAge)>2048)return false;
  WindowPrediction w;if(!predictWindowWithTxDelay(best->advert.sync,best->gp2,gp2,500000,best->advert.txDelayUs,w))return false;
  const uint32_t elapsed=65536-w.waitMicroseconds;
  out.generation=gen;out.sourceAgeUs=age;memcpy(out.source,best->advert.sender,6);
  auto &e=out.election;memcpy(e.masterAddress,best->advert.election.masterAddress,6);
  memcpy(e.syncAddress,best->advert.sender,6);memcpy(e.selfAddress,best->advert.sender,6);
  e.masterMetric=metric;e.masterCounter=counter;e.height=height+1;e.isSelfMaster=false;
  out.aw=uint16_t(w.nextBoundarySequence-4+elapsed/16384);out.down=uint16_t(64-elapsed/1024);
  return true;
 }
 bool build(uint32_t gen,uint32_t gp2,uint64_t hostNs,uint8_t subtype,uint16_t sequence,
            uint8_t *bytes,size_t capacity,size_t &used,FixedControlView &proof)const{
  used=0;proof={};FixedControlView v;if(!view(gen,gp2,hostNs,v))return false;
  ActionParameters p={};memcpy(p.source,local,6);p.subtype=subtype;p.sequence=sequence;
  p.phyTransmitTime=p.targetTransmitTime=gp2;
  auto &s=p.sync;s.nextChannel=s.masterChannel=6;s.awPeriod=16;s.presenceMode=4;s.afPeriod=110;
  s.flags=0x1800;s.awExtensionLength=s.awCommonLength=16;
  s.minExtension=s.maxMulticastExtension=s.maxUnicastExtension=s.maxActionExtension=3;
  s.nextAwSequence=s.apAlignment=v.aw;s.txDownCounter=v.down;
  s.remainingAwLength=64-v.down<16?16-(64-v.down):0;
  memcpy(s.masterAddress,v.election.masterAddress,6);
  p.channels.count=16;p.channels.steps=4;p.channels.encoding=3;p.channels.fill=0xffff;
  for(auto &c:p.channels.entries)c={6,81};
  auto &e=p.election;e.v2=true;memcpy(e.masterAddress,v.election.masterAddress,6);
  memcpy(e.syncAddress,v.election.syncAddress,6);e.height=v.election.height;
  e.masterMetric=v.election.masterMetric;e.masterCounter=v.election.masterCounter;e.selfMetric=60;e.selfCounter=0;
  if(!buildControlAction(p,bytes,capacity,used))return false;proof=v;return true;
 }
};
}
