// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "SingleFrameAdapter.hpp"
#include "Channel6Policy.hpp"
#include "ControlTxEvidence.hpp"
// Entry-specific authorities share this single-descriptor DMA transport.
struct ItlIbssTxPort {
 struct Buffer {
  uint64_t commandAddress=0,scratchAddress=0;ItlIbss::Segment segments[18]={};unsigned count=0;
  const uint8_t *frame=nullptr;size_t length=0;uint8_t storage[2304]={};mbuf_t m=nullptr;
 };
 ItlIwm *hal;const bool mdns;const uint64_t windowEnd;ItlIbss::SingleFrameAdapter<ItlIbssTxPort> adapter;
 unsigned rejectionDetail=0,calls=0;bool retiring=false,stopEvidence=false,mismatch=false;mbuf_t held=nullptr;
 explicit ItlIbssTxPort(ItlIwm *h,bool multicast=false,uint64_t end=0):hal(h),mdns(multicast),windowEnd(end),adapter(*this,multicast){}
 void record(const char *name,uint32_t value){
  if(!hal->awdlProbeCounters)return;using D=ItlIbss::ExperimentObservation;
  if(!strcmp(name,"AWDLIbssTxRejectStage")) {
   // First adapter failure only; no addresses or hardware reads in this record.
   using E=ItlIbss::ExperimentRows;E::Row row;const auto us=now()/1000;auto sc=&hal->com;
   row.v[E::Kind]=38;row.v[E::Valid]=1;row.v[E::Code]=value;row.v[E::FrameControl]=hal->awdlBatch.run;row.v[E::Result]=rejectionDetail;
   row.v[E::Generation]=generation();row.v[E::HostUsLow]=uint32_t(us);row.v[E::HostUsHigh]=uint32_t(us>>32);
   row.v[E::Flags]=sc->sc_flags;row.v[E::MacContext]=uint32_t(sc->sc_ic.ic_state);row.v[E::Length]=uint32_t(sc->ns_nstate);
   const auto &d=hal->awdlProbeCounters->ibss;row.v[E::Channel]=d.get(D::TargetChannel);
   row.v[E::Status]=d.get(D::MccDecoded);row.v[E::Detail0]=d.get(D::MccGeneration);row.v[E::Detail1]=d.get(D::MccFlags6);
   hal->awdlProbeCounters->ibss.control.append(row);return;
  }
  if(!strcmp(name,"AWDLIbssTxVersion")){hal->awdlProbeCounters->ibss.set(D::TxProbeVersion,value);return;}
  if(!strcmp(name,"AWDLIbssTxStartAccepted")){hal->awdlProbeCounters->ibss.set(D::TxAccepted,value);return;}
  if(!strcmp(name,"AWDLIbssTxSubmitted")){hal->awdlProbeCounters->ibss.set(D::TxSubmitted,value);return;}
  if(!strcmp(name,"AWDLIbssTxCompleted")){hal->awdlProbeCounters->ibss.set(D::TxCompleted,value);return;}
  if(!strcmp(name,"AWDLIbssTxStatus")){hal->awdlProbeCounters->ibss.set(D::TxStatus,value);return;}
  if(!strcmp(name,"AWDLIbssTxReclaimed")){hal->awdlProbeCounters->ibss.set(D::TxReclaimed,value);return;}
  if(!strcmp(name,"AWDLIbssTxMismatch")){hal->awdlProbeCounters->ibss.set(D::TxMismatch,value);return;}
  if(!strcmp(name,"AWDLIbssTxCancelRequested")){hal->awdlProbeCounters->ibss.set(D::TxCancelled,value);return;}
  if(!strcmp(name,"AWDLIbssTxGeneration")){hal->awdlProbeCounters->ibss.set(D::TxGeneration,value);return;}
  if(!strcmp(name,"AWDLIbssTxFlushRequested")){hal->awdlProbeCounters->ibss.set(D::TxFlushRequested,value);return;}
  if(!strcmp(name,"AWDLIbssTxFlushFailed")){hal->awdlProbeCounters->ibss.set(D::TxFlushFailed,value);return;}
  if(!strcmp(name,"AWDLIbssTxDeadlineExpired")){hal->awdlProbeCounters->ibss.set(D::TxDeadlineExpired,value);return;}
  if(!strcmp(name,"AWDLIbssTxStopReclaimed")){hal->awdlProbeCounters->ibss.set(D::TxStopReclaimed,value);return;}
 }
 bool inGate(){return hal->getMainWorkLoop()->inGate();}
 uint32_t generation(){return uint32_t(hal->com.sc_generation);}
 uint64_t now(){return itlAwdlRocNowNs();}
 bool refuse(unsigned detail){rejectionDetail=detail;return false;}
 static unsigned radioRejection(ItlIwm *hal,bool requireLease){
#ifndef AIRPORT_VTD
  return (1);
#else
  auto sc=&hal->com;auto &state=hal->awdlIbssExclusive;const auto &s=state.session;
  if(!hal->getMainWorkLoop()->inGate())return (10);
  if(hal->dmaDetaching)return (12);
  if(!hal->awdlProbeCounters)return (13);
  if(!state.ownsRadio)return (14);
  if(state.operation)return (15);
  if(state.resetRequested)return (16);
  if(state.phase!=ItlIbss::ExclusivePhase::FirmwareReady)return (17);
  if(!s.valid())return (18);
  if(s.needsReset)return (19);
  if(s.commandInFlight)return (20);
  if(!s.macAdded)return (21);
  if(!s.binding)return (22);
  if(!s.bcast)return (23);
  if(!s.mcast)return (24);
  if(!s.bcastQueue)return (25);
  if(!s.mcastQueue)return (26);
  if(!s.quotaConfigured)return (27);
  if(uint32_t(hal->com.sc_generation)!=s.generation)return (28);
  if(requireLease && itlAwdlRocNowNs()>=state.deadline)return (29);
  if(s.mac.channel!=6)return (30);
  if(!sc->sc_pcitag)return (31);
  if(!(sc->sc_pcitag->configRead16(kIOPCIConfigCommand)&kIOPCICommandBusMaster))return (32);
  if(!sc->sc_uc.uc_intr)return (33);
  if(!sc->sc_uc.uc_ok)return (34);
  if(sc->sc_uc_current!=IWM_UCODE_TYPE_REGULAR)return (35);
  if(!(sc->sc_ic.ic_if.if_flags&IFF_UP))return (36);
  if(sc->sc_ic.ic_state!=IEEE80211_S_INIT)return (37);
  if(sc->ns_nstate!=IEEE80211_S_INIT)return (38);
  if(hal->awdlScanSubmitting)return (39);
  if(hal->awdlAuxTx.pending)return (40);
  if(ItlAwdl::gEngine.running)return (41);
  if((sc->sc_flags&(IWM_FLAG_HW_ERR|IWM_FLAG_RFKILL|IWM_FLAG_SHUTDOWN|IWM_FLAG_SCANNING|IWM_FLAG_BGSCAN|IWM_FLAG_MAC_ACTIVE|IWM_FLAG_BINDING_ACTIVE|IWM_FLAG_STA_ACTIVE|IWM_FLAG_TE_ACTIVE)))return (42);
  if(!isset(sc->sc_enabled_capa,IWM_UCODE_TLV_CAPA_DQA_SUPPORT))return (43);
  if(!isset(sc->sc_ucode_api,IWM_UCODE_TLV_API_STA_TYPE))return (44);
  auto phy=&sc->sc_phyctxt[s.resources.phy];
  if(phy->id!=s.resources.phy)return (60);
  if(phy->color!=s.phyColor)return (61);
  if(!phy->channel)return (62);
  if(ieee80211_chan2ieee(&sc->sc_ic,phy->channel)!=6)return (63);
  if(phy->channel->ic_freq!=2437)return (64);
  if((phy->channel->ic_flags&ItlIbss::Channel6NativeDenied))return (65);
  if((phy->channel->ic_flags&ItlIbss::Channel6NativeRequired)!=ItlIbss::Channel6NativeRequired)return (66);
  using D=ItlIbss::ExperimentObservation;const auto &d=hal->awdlProbeCounters->ibss;
  ItlIbss::MccChannelProfile m;m.decoded=d.get(D::MccProfileVersion)==1&&d.get(D::MccDecoded);m.generation=d.get(D::MccGeneration);
  m.status=d.get(D::MccStatus);m.present=uint8_t(d.get(D::MccPresent));m.flags6=d.get(D::MccFlags6);
  const auto c=&sc->sc_ic.ic_channels[6];
  if(!ItlIbss::channel6ProfileAllowed(6,c->ic_freq,c->ic_flags,d.get(D::NvmFlags6),d.get(D::NvmChannelFlagsVersion)==1,m,uint32_t(hal->com.sc_generation)))return (80);
  return 0;
#endif
 }
 bool healthy(){rejectionDetail=retiring?11:radioRejection(hal,true);if(!rejectionDetail&&mdns&&(!hal->awdlNativeDiscoveryReady()||hal->awdlNativeDiscovery.port!=this||!hal->awdlNativeDiscovery.open||now()>=windowEnd))rejectionDetail=129;return !rejectionDetail;}
 bool ringIdle(unsigned slot){
  if(slot>=IWM_TX_RING_COUNT)return false;auto sc=&hal->com;auto ring=&sc->txq[3];auto data=&ring->data[slot];
  return ring->desc && ring->cmd && sc->sched_dma.vaddr && !ring->queued && ring->cur==int(slot) && ring->tail==int(slot) && !data->m && !data->in && data->map && data->cmd_paddr && data->scratch_paddr;
 }
 bool snapshot(ItlIbss::TxContext &c){
  if(!healthy())return false;auto sc=&hal->com;auto &s=hal->awdlIbssExclusive.session;
  c.resources=s.resources;c.generation=generation();c.slot=unsigned(sc->txq[3].cur);c.now=now();c.leaseEnd=hal->awdlIbssExclusive.deadline;
  uint8_t valid=hal->iwm_fw_valid_tx_ant(sc);c.antenna=uint8_t(valid&uint8_t(-valid));// Actual exclusive channel6 is 2.4GHz management: native coex.cpp priority3.
  // Do not derive this from the old ordinary-STA ic_bss channel.
  if(hal->awdlNativeSearch.receiveValidation()&&c.leaseEnd-c.now>20000000000ULL)c.leaseEnd=c.now+20000000000ULL;
  if(mdns&&windowEnd<c.leaseEnd)c.leaseEnd=windowEnd;
  c.coexistPriority=3;c.healthy=true;c.ringEmpty=ringIdle(c.slot);if(!c.antenna)return refuse(90);if(!c.ringEmpty)return refuse(91);return true;
 }
 bool revalidate(const ItlIbss::TxContext &c,uint64_t &time){
  time=now();if(!healthy())return false;
  if(hal->awdlIbssDataActive()&&!hal->awdlIbssIdentityWindowValid())return refuse(125);
  const auto &native=hal->awdlNativeControlTx;
  if(native.port==this&&(!native.active()||!hal->awdlNativeSearchEnabled()||
     !hal->awdlNativeSearch.matches(native.epoch)||native.generation!=generation()||
     native.radioLeaseEndNs!=hal->awdlIbssExclusive.deadline||
     native.leaseEndNs!=hal->awdlNativeSearch.deadlineNs||time>=native.leaseEndNs||
     native.leaseEndNs-time<ItlAwdl::kSelfControlHorizonNs))return refuse(126);
  if(native.port==this&&native.periodic&&(!native.sampleHostNs||time<native.sampleHostNs||
     time-native.sampleHostNs>2000000ULL))return refuse(127);
  if(native.port==this&&native.nativeRoleAccepted&&!hal->awdlNativeSyncRoleReady())return refuse(128);
  if(c.generation!=generation())return refuse(120);
  uint64_t expectedEnd=hal->awdlIbssExclusive.deadline;
  if(expectedEnd<c.now)return refuse(121);
  if(hal->awdlNativeSearch.receiveValidation()&&expectedEnd-c.now>20000000000ULL)expectedEnd=c.now+20000000000ULL;
  if(mdns&&windowEnd<expectedEnd)expectedEnd=windowEnd;
  if(c.leaseEnd!=expectedEnd)return refuse(121);
  if(!ringIdle(c.slot))return refuse(122);
  const auto &a=c.resources;const auto &b=hal->awdlIbssExclusive.session.resources;
  if(!(a.mac==b.mac&&a.tsf==b.tsf&&a.phy==b.phy&&a.mcastSta==b.mcastSta&&a.bcastSta==b.bcastSta&&a.mcastQueue==b.mcastQueue&&a.bcastQueue==b.bcastQueue&&a.exclusivelyReserved==b.exclusivelyReserved))return refuse(123);
  if((hal->iwm_fw_valid_tx_ant(&hal->com)&c.antenna)!=c.antenna)return refuse(124);
  return true;
 }
 bool prepareBuffer(const uint8_t *frame,size_t length,Buffer &b){
  rejectionDetail=0;
#ifndef AIRPORT_VTD
  return refuse(1);
#else
  if(length>sizeof(b.storage) || length<40)return refuse(100);
  if(held)return refuse(101);
  memcpy(b.storage,frame,length);b.frame=b.storage;b.length=length;
  uint8_t local[6];if(!hal->awdlIbssLocalAddress(local)||memcmp(frame+10,local,6))return refuse(102);
  if(memcmp(frame+16,hal->awdlIbssExclusive.session.mac.bssid,6))return refuse(103);
  unsigned chunks=1;
  if(mbuf_allocpacket(MBUF_DONTWAIT,length,&chunks,&b.m) || !b.m)return refuse(104);
  held=b.m;
  if(mbuf_next(b.m))return refuse(105);
  if(mbuf_maxlen(b.m)<length)return refuse(106);
  mbuf_setlen(b.m,length);mbuf_pkthdr_setlen(b.m,length);memcpy(mbuf_data(b.m),b.storage,length);mbuf_adj(b.m,24);
  if(mbuf_len(b.m)!=length-24 || mbuf_pkthdr_len(b.m)!=length-24)return refuse(107);
  auto ring=&hal->com.txq[3];const unsigned slot=unsigned(ring->cur);if(!ringIdle(slot))return refuse(108);
  auto data=&ring->data[slot];IOPhysicalSegment segments[18];unsigned count=bus_dmamap_packet(data->map,b.m,segments,18,false);
  if(!count)return refuse(109);
  if(count>18)return refuse(110);
  b.commandAddress=data->cmd_paddr;b.scratchAddress=data->scratch_paddr;b.count=count;
  for(unsigned i=0;i<count;++i){if(segments[i].length>UINT32_MAX)return refuse(111);b.segments[i]={segments[i].location,uint32_t(segments[i].length)};}
  return true;
#endif
 }
 void discardBuffer(Buffer &b){auto m=b.m;b.m=nullptr;held=nullptr;if(m)mbuf_freem(m);}
 void stage(const ItlIbss::TxContext &c,Buffer &b,const iwm_device_cmd &command,const iwm_tfd &tfd,uint16_t scheduler){
  auto sc=&hal->com;auto ring=&sc->txq[3];auto data=&ring->data[c.slot];
  memcpy(&ring->cmd[c.slot],&command,sizeof(command));memcpy(&ring->desc[c.slot],&tfd,sizeof(tfd));
  data->m=b.m;data->in=nullptr;data->totlen=int(b.length);data->fc=htole16(ItlAwdl::dataLe16(b.frame));data->txrate=data->txmcs=0;memset(&data->info,0,sizeof(data->info));
  auto table=static_cast<iwm_agn_scd_bc_tbl*>(sc->sched_dma.vaddr);auto bc=htole16(scheduler);table[3].tfd_offset[c.slot]=bc;
  if(c.slot<IWM_TFD_QUEUE_SIZE_BC_DUP)table[3].tfd_offset[IWM_TFD_QUEUE_SIZE_MAX+c.slot]=bc;
  ring->cur=(c.slot+1)%IWM_TX_RING_COUNT;++ring->queued;__sync_synchronize();
 }
 void doorbell(const ItlIbss::TxContext &){IWM_WRITE(&hal->com,IWM_HBUS_TARG_WRPTR,(3<<8)|hal->com.txq[3].cur);record("AWDLIbssTxSubmitted",1);}
 bool matching(unsigned slot){if(slot>=IWM_TX_RING_COUNT)return false;auto ring=&hal->com.txq[3];return held&&ring->data[slot].m==held&&ring->queued==1&&ring->tail==int(slot)&&ring->cur==int((slot+1)%IWM_TX_RING_COUNT);}
 void reclaim(const ItlIbss::TxContext &c,Buffer &b){
  if(!matching(c.slot)){mismatch=true;record("AWDLIbssTxMismatch",1);hal->awdlIbssExclusive.resetRequested=true;return;}
  auto sc=&hal->com;auto ring=&sc->txq[3];auto data=&ring->data[c.slot];hal->iwm_reset_sched(sc,3,int(c.slot),mdns?c.resources.mcastSta:c.resources.bcastSta);
  ring->queued=0;ring->tail=ring->cur;b.m=nullptr;held=nullptr;record("AWDLIbssTxReclaimed",1);hal->iwm_txd_done(sc,data);
 }
 bool flush(uint32_t mask){
  if(!inGate() || hal->getMainWorkLoop()->onThread() || mask!=(1u<<3))return false;
  ItlIbss::CommandScope authority(hal->awdlIbssExclusive.commandAuthority,reinterpret_cast<uintptr_t>(current_thread()));
  if(!authority)return false;record("AWDLIbssTxFlushRequested",1);bool ok=!hal->iwm_flush_tx_path(&hal->com,int(mask));if(!ok)record("AWDLIbssTxFlushFailed",1);return ok;
 }
 bool stopProof(const ItlIbss::TxContext &c){return stopEvidence&&inGate()&&hal->com.sc_pcitag&&!(hal->com.sc_pcitag->configRead16(kIOPCIConfigCommand)&kIOPCICommandBusMaster)&&matching(c.slot);}
};
bool ItlIwm::awdlIbssManagementDrained(){
 auto p=awdlIbssTxPort;if(!p||!awdlProbeCounters||p->hal!=this||!getMainWorkLoop()->inGate()||
    p->calls||p->retiring||p->mismatch||p->held||p->adapter.pending())return false;
 using D=ItlIbss::ExperimentObservation;auto &d=awdlProbeCounters->ibss;
 return d.get(D::TxGeneration)==uint32_t(com.sc_generation)&&d.get(D::TxCompleted)==1&&
  d.get(D::TxReclaimed)==1&&!d.get(D::TxStopReclaimed)&&!d.get(D::TxDeadlineExpired)&&
  (d.get(D::TxStatus)&255)==1;
}
bool ItlIwm::awdlIbssTxStart(const uint8_t *frame,size_t length){
 if(!getMainWorkLoop()->inGate() || getMainWorkLoop()->onThread() || dmaDetaching || awdlIbssTxPort || awdlIbssPeerActive() ||
    !awdlBatch.enabled || !ItlIbss::batchTxMode(awdlBatch.mode) || !awdlBatch.txUsed)return false;
 auto p=new ItlIbssTxPort(this);if(!p)return false;awdlIbssTxPort=p;++p->calls;p->record("AWDLIbssTxVersion",awdlBatch.mode==ItlIbss::BatchMode::ControlAdvertisement?2:1);p->record("AWDLIbssTxGeneration",uint32_t(com.sc_generation));bool result=p->adapter.start(frame,length);p->record("AWDLIbssTxStartAccepted",result);--p->calls;if(p->retiring)awdlIbssTxDispose();return result;
}
int ItlIwm::awdlIbssTxComplete(unsigned queue,unsigned slot,uint32_t status){
 if(queue==2){const int handled=awdlListenerTxComplete(queue,slot,status);if(handled)return handled;}
 auto p=awdlIbssTxPort;if(!p || queue!=3)return 0;if(!getMainWorkLoop()->inGate())return 1;
 ++p->calls;
 if(p->adapter.pending()&&!p->matching(slot)){p->mismatch=true;p->record("AWDLIbssTxMismatch",1);awdlIbssExclusive.resetRequested=true;--p->calls;return 1;}
 auto result=p->adapter.complete(uint32_t(com.sc_generation),queue,slot);if(result==ItlIbss::SingleFrameAdapter<ItlIbssTxPort>::Completion::Reclaim){p->record("AWDLIbssTxStatus",status);p->record("AWDLIbssTxCompleted",1);getMainCommandGate()->commandWakeup(&awdlIbssWorkPending,false);}--p->calls;if(p->retiring)awdlIbssTxDispose();
 using C=ItlIbss::SingleFrameAdapter<ItlIbssTxPort>::Completion;return result==C::Reclaim?2:result==C::Unowned?0:1;
}
void ItlIwm::awdlIbssTxTick(){auto p=awdlIbssTxPort;if(!p)return;++p->calls;if(p->adapter.pending() && p->now()>=p->adapter.pendingDeadline() && !awdlProbeCounters->ibss.get(ItlIbss::ExperimentObservation::TxFlushRequested))p->record("AWDLIbssTxDeadlineExpired",1);p->adapter.tick();--p->calls;if(p->retiring)awdlIbssTxDispose();}
void ItlIwm::awdlIbssTxCancel(){awdlListenerTxCancel();auto p=awdlIbssTxPort;if(!p)return;++p->calls;if(p->adapter.pending())p->record("AWDLIbssTxCancelRequested",1);p->adapter.cancel();--p->calls;if(p->retiring)awdlIbssTxDispose();}
void ItlIwm::awdlIbssTxStopped(){awdlListenerTxStopped();auto p=awdlIbssTxPort;if(!p)return;++p->calls;p->stopEvidence=true;p->retiring=true;const bool pending=p->adapter.pending();p->adapter.hardwareStopped();if(pending&&!p->adapter.pending())p->record("AWDLIbssTxStopReclaimed",1);--p->calls;awdlIbssTxDispose();}
void ItlIwm::awdlIbssTxDispose(){auto p=awdlIbssTxPort;if(!p)return;p->retiring=true;if(p->calls || p->adapter.pending() || p->held || p->mismatch)return;awdlIbssTxPort=nullptr;delete p;}

int ItlIwm::awdlIbssTxProbeOnce(){
 using D=ItlIbss::ExperimentObservation;
 if(!awdlProbeCounters || !getMainWorkLoop()->inGate() || getMainWorkLoop()->onThread() ||
    !awdlBatch.enabled || !ItlIbss::batchTxMode(awdlBatch.mode) || !awdlBatch.txUsed)return EPERM;
 auto &d=awdlProbeCounters->ibss;if(d.get(D::TxAttempted))return EBUSY;
 const bool advertisement=awdlBatch.mode==ItlIbss::BatchMode::ControlAdvertisement;
 const uint32_t generation=uint32_t(com.sc_generation);const auto lease=awdlIbssExclusive.deadline;
 d.set(D::TxProbeVersion,advertisement?2:1);d.set(D::TxAttempted,1);d.set(D::TxRun,awdlBatch.run);d.set(D::TxGeneration,generation);d.set(D::TxPayloadBytes,advertisement?227:40);
 uint8_t frame[227];
 const uint64_t before=itlAwdlRocNowNs();if(!iwm_nic_lock(&com))return EBUSY;
 const auto gp2=iwm_read_prph(&com,IWM_DEVICE_SYSTEM_TIME_REG);iwm_nic_unlock(&com);
 const uint64_t after=itlAwdlRocNowNs();
 if(!advertisement){if(!ItlAwdl::buildAuxTxProbe(com.sc_ic.ic_myaddr,gp2,frame))return EINVAL;}
 else {
  if(after<before || after-before>2000000ull)return EBUSY;
  ItlAwdl::SelfControlInput input;for(unsigned n=0;n<6;++n)input.source[n]=com.sc_ic.ic_myaddr[n];
  input.generation=generation;input.epochGp2=gp2;input.epochHostNs=before+(after-before)/2;
  const auto nextBefore=itlAwdlRocNowNs();if(!iwm_nic_lock(&com))return EBUSY;
  input.nowGp2=iwm_read_prph(&com,IWM_DEVICE_SYSTEM_TIME_REG);iwm_nic_unlock(&com);
  const auto nextAfter=itlAwdlRocNowNs();if(nextAfter<nextBefore || nextAfter-nextBefore>2000000ull)return EBUSY;
  input.nowHostNs=nextBefore+(nextAfter-nextBefore)/2;input.currentGeneration=uint32_t(com.sc_generation);input.leaseEndNs=lease;
  input.sequence=uint16_t(awdlBatch.run&4095);input.advertisementCounter=awdlBatch.run;
  if(awdlIbssExclusive.deadline!=lease)return ESTALE;
  size_t used=0;ItlAwdl::SelfControlEvidence proof;
  if(!ItlAwdl::buildSelfControlAdvertisement(input,frame,sizeof(frame),used,proof) || used!=227)return EINVAL;
  ItlIbss::appendControlTxEvidence(d,input,frame,awdlBatch.run);
  if(!ItlIbss::controlTxEvidenceMatches(d,awdlBatch.run,generation))return EIO;
 }
 return awdlIbssTxStart(frame,advertisement?227:40)?0:EIO;
}
