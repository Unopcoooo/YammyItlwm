#pragma once
#include "SingleFrameAdapter.hpp"
#include "Channel6Policy.hpp"
struct ItlListenerTxPort {
 struct Buffer {
  uint64_t commandAddress=0,scratchAddress=0;ItlIbss::Segment segments[18]={};unsigned count=0;
  const uint8_t *frame=nullptr;size_t length=0;uint8_t storage[2304]={};mbuf_t m=nullptr;
 };
 ItlIwm *hal;ItlIbss::SingleFrameAdapter<ItlListenerTxPort,true> adapter;
 unsigned rejectionDetail=0,calls=0;bool retiring=false,stopEvidence=false,mismatch=false;mbuf_t held=nullptr;
 explicit ItlListenerTxPort(ItlIwm *h):hal(h),adapter(*this){}
 void record(const char *name,uint32_t value){
  if(!strcmp(name,"AWDLIbssTxRejectStage"))hal->awdlIbssExperimentEvent(57,9,int(value),rejectionDetail,0);
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
  if(!hal->awdlListenerTxActive()||!state.ownsRadio)return (14);
  if(state.operation)return (15);
  if(state.resetRequested)return (16);
  if(state.phase!=ItlIbss::ExclusivePhase::FirmwareReady)return (17);
  if(!s.valid())return (18);
  if(s.needsReset)return (19);
  if(s.commandInFlight)return (20);
  if(!s.macAdded)return (21);
  if(!s.binding)return (22);
  if(s.bcast)return (23);
  if(!s.mcast)return (24);
  if(s.bcastQueue)return (25);
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
 bool healthy(){rejectionDetail=retiring?11:radioRejection(hal,true);return !rejectionDetail;}
 bool ringIdle(unsigned slot){
  if(slot>=IWM_TX_RING_COUNT)return false;auto sc=&hal->com;auto ring=&sc->txq[2];auto data=&ring->data[slot];
  return ring->desc && ring->cmd && sc->sched_dma.vaddr && !ring->queued && ring->cur==int(slot) && ring->tail==int(slot) && !data->m && !data->in && data->map && data->cmd_paddr && data->scratch_paddr;
 }
 bool snapshot(ItlIbss::TxContext &c){
  if(!healthy())return false;auto sc=&hal->com;auto &s=hal->awdlIbssExclusive.session;
  c.resources=s.resources;c.generation=generation();c.slot=unsigned(sc->txq[2].cur);c.now=now();c.leaseEnd=hal->awdlIbssExclusive.deadline;
  uint8_t valid=hal->iwm_fw_valid_tx_ant(sc);c.antenna=uint8_t(valid&uint8_t(-valid));// Actual exclusive channel6 is 2.4GHz management: native coex.cpp priority3.
  // Do not derive this from the old ordinary-STA ic_bss channel.
  c.coexistPriority=3;c.healthy=true;c.ringEmpty=ringIdle(c.slot);if(!c.antenna)return refuse(90);if(!c.ringEmpty)return refuse(91);return true;
 }
 bool revalidate(const ItlIbss::TxContext &c,uint64_t &time){
  time=now();if(!healthy())return false;
  if(memcmp(hal->awdlIbssExclusive.session.mac.source,hal->com.sc_ic.ic_myaddr,6))return refuse(126);
  if(!hal->awdlListenerTxWindowValid())return refuse(125);
  if(c.generation!=generation())return refuse(120);
  if(c.leaseEnd!=hal->awdlIbssExclusive.deadline)return refuse(121);
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
  if(memcmp(frame+10,hal->com.sc_ic.ic_myaddr,6))return refuse(102);
  const uint8_t awdlBssid[]={0,0x25,0,0xff,0x94,0x73};if(memcmp(frame+16,awdlBssid,6))return refuse(103);
  unsigned chunks=1;
  if(mbuf_allocpacket(MBUF_DONTWAIT,length,&chunks,&b.m) || !b.m)return refuse(104);
  held=b.m;
  if(mbuf_next(b.m))return refuse(105);
  if(mbuf_maxlen(b.m)<length)return refuse(106);
  mbuf_setlen(b.m,length);mbuf_pkthdr_setlen(b.m,length);memcpy(mbuf_data(b.m),b.storage,length);mbuf_adj(b.m,24);
  if(mbuf_len(b.m)!=length-24 || mbuf_pkthdr_len(b.m)!=length-24)return refuse(107);
  auto ring=&hal->com.txq[2];const unsigned slot=unsigned(ring->cur);if(!ringIdle(slot))return refuse(108);
  auto data=&ring->data[slot];IOPhysicalSegment segments[18];unsigned count=bus_dmamap_packet(data->map,b.m,segments,18,false);
  if(!count)return refuse(109);
  if(count>18)return refuse(110);
  b.commandAddress=data->cmd_paddr;b.scratchAddress=data->scratch_paddr;b.count=count;
  for(unsigned i=0;i<count;++i){if(segments[i].length>UINT32_MAX)return refuse(111);b.segments[i]={segments[i].location,uint32_t(segments[i].length)};}
  __sync_synchronize();
  if(!data->map->dma.vaddr||data->map->dma.size<length-24||memcmp(data->map->dma.vaddr,b.storage+24,length-24))return refuse(112);
  return true;
#endif
 }
 void discardBuffer(Buffer &b){auto m=b.m;b.m=nullptr;held=nullptr;if(m)mbuf_freem(m);}
 void stage(const ItlIbss::TxContext &c,Buffer &b,const iwm_device_cmd &command,const iwm_tfd &tfd,uint16_t scheduler){
  auto sc=&hal->com;auto ring=&sc->txq[2];auto data=&ring->data[c.slot];
  memcpy(&ring->cmd[c.slot],&command,sizeof(command));memcpy(&ring->desc[c.slot],&tfd,sizeof(tfd));
  data->m=b.m;data->in=nullptr;data->totlen=int(b.length);data->fc=htole16(ItlAwdl::dataLe16(b.storage));data->txrate=data->txmcs=0;memset(&data->info,0,sizeof(data->info));
  auto table=static_cast<iwm_agn_scd_bc_tbl*>(sc->sched_dma.vaddr);auto bc=htole16(scheduler);table[2].tfd_offset[c.slot]=bc;
  if(c.slot<IWM_TFD_QUEUE_SIZE_BC_DUP)table[2].tfd_offset[IWM_TFD_QUEUE_SIZE_MAX+c.slot]=bc;
  ring->cur=(c.slot+1)%IWM_TX_RING_COUNT;++ring->queued;__sync_synchronize();
 }
 void doorbell(const ItlIbss::TxContext &){IWM_WRITE(&hal->com,IWM_HBUS_TARG_WRPTR,(2<<8)|hal->com.txq[2].cur);record("AWDLIbssTxSubmitted",1);}
 bool matching(unsigned slot){if(slot>=IWM_TX_RING_COUNT)return false;auto ring=&hal->com.txq[2];return held&&ring->data[slot].m==held&&ring->queued==1&&ring->tail==int(slot)&&ring->cur==int((slot+1)%IWM_TX_RING_COUNT);}
 void reclaim(const ItlIbss::TxContext &c,Buffer &b){
  if(!matching(c.slot)){mismatch=true;record("AWDLIbssTxMismatch",1);hal->awdlIbssExclusive.resetRequested=true;return;}
  auto sc=&hal->com;auto ring=&sc->txq[2];auto data=&ring->data[c.slot];hal->iwm_reset_sched(sc,2,int(c.slot),c.resources.mcastSta);
  ring->queued=0;ring->tail=ring->cur;b.m=nullptr;held=nullptr;record("AWDLIbssTxReclaimed",1);hal->iwm_txd_done(sc,data);
 }
 bool flush(uint32_t mask){
  if(!inGate() || hal->getMainWorkLoop()->onThread() || mask!=(1u<<2))return false;
  ItlIbss::CommandScope authority(hal->awdlIbssExclusive.commandAuthority,reinterpret_cast<uintptr_t>(current_thread()));
  if(!authority)return false;record("AWDLIbssTxFlushRequested",1);bool ok=!hal->iwm_flush_tx_path(&hal->com,int(mask));if(!ok)record("AWDLIbssTxFlushFailed",1);return ok;
 }
 bool stopProof(const ItlIbss::TxContext &c){return stopEvidence&&inGate()&&hal->com.sc_pcitag&&!(hal->com.sc_pcitag->configRead16(kIOPCIConfigCommand)&kIOPCICommandBusMaster)&&matching(c.slot);}
};
static ItlListenerTxPort *itlListenerTxPort=nullptr;
