// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "SelfControlAdvertisement.hpp"
#include "IbssExperiment.hpp"
namespace ItlIbss {
inline uint32_t controlTxDigest(const uint8_t *p,size_t n){uint32_t h=2166136261u;for(size_t i=0;i<n;++i)h=(h^p[i])*16777619u;return h;}
constexpr unsigned ControlTxFields[10]={ExperimentRows::Result,ExperimentRows::Flags,ExperimentRows::Channel,ExperimentRows::Gp2,ExperimentRows::MacContext,ExperimentRows::Status,ExperimentRows::Length,ExperimentRows::Detail0,ExperimentRows::Detail1,ExperimentRows::Phase};
// Exactly 8 bounded rows: two input records and six raw-frame chunks. Local source only.
inline void appendControlTxEvidence(ExperimentObservation &d,const ItlAwdl::SelfControlInput &i,const uint8_t frame[227],uint32_t run){
 using E=ExperimentRows;E::Row r;const auto us=i.nowHostNs/1000;
 r.v[E::Kind]=39;r.v[E::Valid]=1;r.v[E::FrameControl]=run;r.v[E::Generation]=i.generation;
 r.v[E::HostUsLow]=uint32_t(us);r.v[E::HostUsHigh]=uint32_t(us>>32);r.v[E::Code]=1;
 r.v[E::Gp2]=i.epochGp2;r.v[E::Detail0]=i.nowGp2;r.v[E::Flags]=i.epochAwSequence;r.v[E::MacContext]=i.sequence;
 r.v[E::Result]=i.selfMetric;r.v[E::Status]=i.advertisementCounter;r.v[E::Channel]=i.subtype;r.v[E::Length]=227;d.control.append(r);
 r.v[E::Code]=2;r.v[E::Result]=uint32_t(i.epochHostNs);r.v[E::Flags]=uint32_t(i.epochHostNs>>32);
 r.v[E::Channel]=uint32_t(i.nowHostNs);r.v[E::Gp2]=uint32_t(i.nowHostNs>>32);
 r.v[E::MacContext]=uint32_t(i.leaseEndNs);r.v[E::Status]=uint32_t(i.leaseEndNs>>32);
 r.v[E::Length]=controlTxDigest(frame,227);r.v[E::Detail0]=227;r.v[E::Detail1]=2;d.control.append(r);
 for(unsigned part=0;part<6;++part){
  r.v[E::Code]=0x100+part;
  for(unsigned f=0;f<10;++f){uint32_t word=0;for(unsigned b=0;b<4;++b){const unsigned offset=part*40+f*4+b;if(offset<227)word|=uint32_t(frame[offset])<<(b*8);}r.v[ControlTxFields[f]]=word;}
  d.control.append(r);
 }
}
inline bool controlTxEvidenceMatches(const ExperimentObservation &d,uint32_t run,uint32_t generation){
 using E=ExperimentRows;E::Row meta[2]={};uint8_t frame[240]={};unsigned counts[8]={};uint32_t serials[8]={};
 for(unsigned n=0;n<E::Capacity;++n){E::Row r;if(!d.control.read(n,r)||r.v[E::Kind]!=39)continue;
  const auto code=r.v[E::Code];const unsigned index=code==1?0:code==2?1:code>=0x100&&code<=0x105?code-0x100+2:8;
  if(index>=8 || ++counts[index]!=1 || r.v[E::Valid]!=1 || r.v[E::FrameControl]!=run || r.v[E::Generation]!=generation)return false;
  serials[index]=r.v[E::Serial];if(index<2)meta[index]=r;
  else for(unsigned f=0;f<10;++f)for(unsigned b=0;b<4;++b)frame[(index-2)*40+f*4+b]=uint8_t(r.v[ControlTxFields[f]]>>(b*8));
 }
 for(unsigned n=0;n<8;++n)if(counts[n]!=1 || (n&&serials[n]<=serials[n-1]))return false;
 if(d.control.lost || meta[0].v[E::Length]!=227 || meta[1].v[E::Detail0]!=227 || meta[1].v[E::Detail1]!=2 || controlTxDigest(frame,227)!=meta[1].v[E::Length])return false;
 for(unsigned n=227;n<240;++n)if(frame[n])return false;
 ItlAwdl::SelfControlInput i;for(unsigned n=0;n<6;++n)i.source[n]=frame[10+n];i.generation=i.currentGeneration=generation;
 i.epochGp2=meta[0].v[E::Gp2];i.nowGp2=meta[0].v[E::Detail0];i.epochAwSequence=uint16_t(meta[0].v[E::Flags]);i.sequence=uint16_t(meta[0].v[E::MacContext]);
 if(meta[0].v[E::Flags]>65535 || meta[0].v[E::MacContext]>4095 || meta[0].v[E::Channel]>255)return false;
 i.selfMetric=meta[0].v[E::Result];i.advertisementCounter=meta[0].v[E::Status];i.subtype=uint8_t(meta[0].v[E::Channel]);
 i.epochHostNs=meta[1].v[E::Result]|uint64_t(meta[1].v[E::Flags])<<32;i.nowHostNs=meta[1].v[E::Channel]|uint64_t(meta[1].v[E::Gp2])<<32;
 i.leaseEndNs=meta[1].v[E::MacContext]|uint64_t(meta[1].v[E::Status])<<32;
 uint8_t expected[227];size_t used=0;ItlAwdl::SelfControlEvidence proof;
 if(!ItlAwdl::buildSelfControlAdvertisement(i,expected,sizeof(expected),used,proof))return false;
 for(unsigned n=0;n<227;++n)if(frame[n]!=expected[n])return false;return true;
}
}
