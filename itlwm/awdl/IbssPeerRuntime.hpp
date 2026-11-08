// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "IbssDataTx.hpp"
// Defined by the trial owner below this runtime; no HAL layout/vtable change.
static bool itlNativeSearchPeerOwned(ItlIwm *);
static bool itlNativeSearchPeerMayTransmit(ItlIwm *,const ItlIbss::PeerGrant &);
static bool itlNativeSearchPeerComplete(ItlIwm *,uint32_t,unsigned,unsigned,uint32_t);
static void itlNativeSearchPeerStopped(ItlIwm *);
static bool itlIbssPeerReplyWindowOpen(ItlIwm *,uint32_t,uint64_t);
static void itlIbssApplyObservedCapabilities(ItlIwm *,ItlIbss::PeerGrant &,unsigned);
static bool itlIbssNativeComplete(ItlIwm *,uint32_t,unsigned,unsigned,uint32_t);
static void itlIbssNativeTick(ItlIwm *);
static void itlIbssNativeStop(ItlIwm *,bool);
static bool itlIbssNativeMayTransmit(ItlIwm *,const ItlIbss::PeerGrant &);
static bool itlIbssNativeRetainPeer(ItlIwm *,uint32_t,uint64_t);
static_assert(IWM_STA_FLG_FAT_EN_20MHZ==0&&IWM_STA_FLG_MIMO_EN_SISO==0&&
 IWM_STA_FLG_MIMO_EN_MIMO2==(1u<<28)&&IWM_STA_FLG_MAX_AGG_SIZE_MSK==(15u<<19)&&
 IWM_STA_FLG_AGG_MPDU_DENS_MSK==(7u<<23),"observed HT flags match this firmware API");
// One fresh-firmware exclusive session, armed by the manual data trial only.
// No boot activation or native data callback. Never use this inventory in coexistence.
struct ItlIbssPeerPort {
 using Buffer=ItlIbssTxPort::Buffer;
 uint8_t request[ItlAwdl::kAwdlDataMax]={};
 ItlIwm *hal;ItlIbss::PeerState peer;ItlIbss::PeerGrant reserved;
 struct CommandTransport {
  ItlIbssPeerPort &p;
  bool ready(const ItlIbss::PeerGrant &g){return p.ready(g);}
  int status(unsigned id,const void *b,size_t n,uint32_t &s){return p.status(id,b,n,s);}
  int pdu(unsigned id,const void *b,size_t n){return p.pdu(id,b,n);}
  int enableQueue(uint8_t s,uint8_t q,unsigned f,unsigned t){return p.enableQueue(s,q,f,t);}
  int disableQueue(uint8_t s,uint8_t q,unsigned t){return p.disableQueue(s,q,t);}
  int drain(const ItlIbss::PeerGrant &g,bool b){return p.drain(g,b);}
  int flush(uint32_t mask){return p.flushCommands(mask);}
  bool queueDrained(const ItlIbss::PeerGrant &g){return p.queueDrained(g);}
  void release(const ItlIbss::PeerGrant &g){p.release(g);}
  bool stopProof(const ItlIbss::PeerGrant &g){return p.stopProof(g);}
 } commandTransport{*this};
 ItlIbss::PeerCommands<CommandTransport> commands;
 ItlIbss::PeerDataTx<ItlIbssPeerPort> data;
 unsigned rejection=0,nativeWindowReason=0,dmaEvidenceSamples=0;
 size_t requestLength=0;
 uint32_t noticeSlot=0,noticeLength=0,noticeCount=0,noticeSsn=UINT32_MAX,noticeStatus=UINT32_MAX;
 mbuf_t held=nullptr;unsigned calls=0;bool stopEvidence=false,retiring=false,mismatch=false;
 explicit ItlIbssPeerPort(ItlIwm *h):hal(h),commands(commandTransport,peer),data(*this,peer){}
 bool inGate(){return hal->getMainWorkLoop()->inGate();}
 bool onWorker(){return inGate()&&!hal->getMainWorkLoop()->onThread();}
 uint64_t now(){return itlAwdlRocNowNs();}
 uint32_t generation(){return uint32_t(hal->com.sc_generation);}
 void fault(){mismatch=true;peer.unknown=true;peer.ready=false;hal->awdlIbssExclusive.resetRequested=true;}
 bool ready(const ItlIbss::PeerGrant &g){
  if(retiring)return reject(21);
  if(!ItlIbss::samePeerGrant(g,reserved))return reject(22);
  const auto guard=ItlIbssTxPort::radioRejection(hal,false);if(guard)return reject(23,guard);
  const auto &s=hal->awdlIbssExclusive.session;const auto &a=g.base;const auto &b=s.resources;
  uint8_t local[6];if(!hal->awdlIbssLocalAddress(local))return reject(24,8);
  const unsigned mismatch=(generation()!=g.generation?1u:0u)|(g.color!=s.mac.color?2u:0u)|
   (memcmp(g.local,s.mac.source,6)?4u:0u)|(memcmp(g.local,local,6)?8u:0u)|
   ((a.mac!=b.mac||a.tsf!=b.tsf||a.phy!=b.phy||a.mcastSta!=b.mcastSta||a.bcastSta!=b.bcastSta||a.mcastQueue!=b.mcastQueue||a.bcastQueue!=b.bcastQueue)?16u:0u);
  return mismatch?reject(24,mismatch):true;
 }
 bool ringIdle(unsigned slot){
  if(!reserved.valid()||slot>=IWM_TX_RING_COUNT)return false;
  auto sc=&hal->com;auto r=&sc->txq[reserved.queue];auto d=&r->data[slot];
  return r->desc&&r->cmd&&sc->sched_dma.vaddr&&!r->queued&&r->cur==int(slot)&&r->tail==int(slot)&&
   !d->m&&!d->in&&d->map&&d->cmd_paddr&&d->scratch_paddr;
 }
 bool reject(unsigned stage,unsigned detail=0){
  if(!rejection){rejection=stage;if(inGate())hal->awdlIbssExperimentEvent(41,stage,int(detail),hal->com.agg_queue_mask,hal->com.sc_flags);}
  return false;
 }
 bool reserve(const uint8_t *frame,size_t length){
  if(!inGate())return reject(1);
  if(hal->getMainWorkLoop()->onThread())return reject(2);
  const auto guard=ItlIbssTxPort::radioRejection(hal,true);if(guard)return reject(3,guard);
  if(!ItlAwdl::ibssDataHeaderLength(frame,length))return reject(4);
  auto sc=&hal->com;const auto &s=hal->awdlIbssExclusive.session;
  // reserveFresh + command authority establish the complete fresh inventory.
  if(s.resources.mac!=1||s.resources.phy!=1||s.resources.tsf!=1||s.resources.mcastSta!=3||s.resources.bcastSta!=4)return reject(5);
  if(sc->agg_queue_mask)return reject(6,sc->agg_queue_mask);
  if(hal->awdlIbssTxPort&&!hal->awdlIbssManagementDrained())return reject(7);
  if(memcmp(frame+10,s.mac.source,6))return reject(8);
  if(memcmp(frame+16,s.mac.bssid,6))return reject(9);
  reserved.base=s.resources;reserved.generation=generation();reserved.color=s.mac.color;
  memcpy(reserved.local,frame+10,6);memcpy(reserved.peer,frame+4,6);reserved.station=5;
  unsigned firstBad=0;
  for(unsigned q=IWM_FIRST_AGG_TX_QUEUE;q<=IWM_LAST_AGG_TX_QUEUE;++q){
   auto r=&sc->txq[q];unsigned bad=(!r->desc?1u:0u)|(!r->cmd?2u:0u)|(r->queued?4u:0u)|(r->cur!=r->tail?8u:0u);
   unsigned badSlot=0;
   if(!bad)for(unsigned i=0;i<IWM_TX_RING_COUNT;++i){const auto &d=r->data[i];bad=(d.m?16u:0u)|(d.in?32u:0u)|(!d.map?64u:0u)|(!d.cmd_paddr?128u:0u)|(!d.scratch_paddr?256u:0u);if(bad){badSlot=i;break;}}
   if(bad){if(!firstBad)firstBad=q|(badSlot<<8)|(bad<<16);continue;}
   reserved.queue=uint8_t(q);reserved.independentlyReserved=true;
   return ready(reserved)?true:reject(11,ItlIbssTxPort::radioRejection(hal,false));
  }
  reserved={};return reject(10,firstBad);
 }

 template<class F> int command(F f){
  if(!ready(reserved)||hal->getMainWorkLoop()->onThread())return EBUSY;
  ItlIbss::CommandScope authority(hal->awdlIbssExclusive.commandAuthority,reinterpret_cast<uintptr_t>(current_thread()));
  if(!authority)return EBUSY;return f();
 }
 int status(unsigned id,const void *p,size_t n,uint32_t &out){const int e=command([&]{return hal->iwm_send_cmd_pdu_status(&hal->com,id,uint16_t(n),p,&out);});hal->awdlIbssExperimentEvent(43,id,e,out,unsigned(n));return e;}
 int pdu(unsigned id,const void *p,size_t n){const int e=command([&]{return hal->iwm_send_cmd_pdu(&hal->com,id,0,uint16_t(n),p);});hal->awdlIbssExperimentEvent(43,id,e,reserved.station|(unsigned(reserved.queue)<<8),unsigned(n));return e;}
 int enableQueue(uint8_t station,uint8_t queue,unsigned fifo,unsigned tid){
  if(station!=reserved.station||queue!=reserved.queue||fifo!=1||tid)return EINVAL;
  const int error=command([&]{auto sc=&hal->com;if(!hal->iwm_nic_lock(sc))return EBUSY;
   const int e=hal->iwm_enable_txq(sc,station,queue,1,0,0,0);hal->iwm_nic_unlock(sc);return e;});
  hal->awdlIbssExperimentEvent(43,IWM_SCD_QUEUE_CFG,error,station|(unsigned(queue)<<8),1);return error;
 }
 int disableQueue(uint8_t station,uint8_t queue,unsigned tid){
  if(station!=reserved.station||queue!=reserved.queue||tid)return EINVAL;
  iwm_scd_txq_cfg_cmd c={};c.scd_queue=queue;c.enable=IWM_SCD_CFG_DISABLE_QUEUE;c.sta_id=station;
  return pdu(IWM_SCD_QUEUE_CFG,&c,sizeof(c));
 }
 int drain(const ItlIbss::PeerGrant &g,bool enabled){
  if(!ItlIbss::samePeerGrant(g,reserved))return EINVAL;
  iwm_add_sta_cmd c={};c.add_modify=1;c.sta_id=g.station;
  ItlIbss::peerWord32(&c.mac_id_n_color,uint32_t(g.base.mac)|(uint32_t(g.color)<<8));
  ItlIbss::peerWord32(&c.station_flags_msk,IWM_STA_FLG_DRAIN_FLOW);
  ItlIbss::peerWord32(&c.station_flags,enabled?IWM_STA_FLG_DRAIN_FLOW:0);
  uint32_t st=0;int e=status(IWM_ADD_STA,&c,sizeof(c),st);return e?e:(st&255)==1?0:EIO;
 }
 // PeerCommands needs errno; PeerDataTx needs a success boolean. Use a separate
 // adapter below so zero (success) is never interpreted as a failed data flush.
 bool flush(uint32_t mask){return !flushCommands(mask);}
 int flushCommands(uint32_t mask){if(mask!=(1u<<reserved.queue))return EINVAL;return command([&]{return hal->iwm_flush_tx_path(&hal->com,int(mask));});}
 bool queueDrained(const ItlIbss::PeerGrant &g){return !data.pending()&&!held&&ready(g)&&ringIdle(unsigned(hal->com.txq[g.queue].cur));}
 void release(const ItlIbss::PeerGrant &g){if(!ItlIbss::samePeerGrant(g,reserved))fault();} // tombstone retained until physical stop
 bool stopProof(const ItlIbss::PeerGrant &g){return stopEvidence&&inGate()&&ItlIbss::samePeerGrant(g,reserved)&&hal->com.sc_pcitag&&!(hal->com.sc_pcitag->configRead16(kIOPCIConfigCommand)&kIOPCICommandBusMaster);}
 bool stopProof(const ItlIbss::PeerTxContext &c){return stopProof(c.grant)&&matching(c.slot);}
 bool snapshot(ItlIbss::PeerTxContext &c){
  rejection=0; // Classify only this admission check, never a prior slot denial.
  if(!ready(reserved))return false;
  if(now()>=hal->awdlIbssExclusive.deadline)return reject(25);
  c.grant=reserved;c.slot=unsigned(hal->com.txq[reserved.queue].cur);c.now=now();c.leaseEnd=hal->awdlIbssExclusive.deadline;
  const uint8_t ant=hal->iwm_fw_valid_tx_ant(&hal->com);c.antenna=uint8_t(ant&uint8_t(-ant));c.priority=3;
  if(!c.antenna)return reject(26);
  if(!ringIdle(c.slot))return reject(27,c.slot);
  return hal->awdlIbssDataMayTransmit(c.grant)?true:reject(28);
 }
 bool revalidate(const ItlIbss::PeerTxContext &c,uint64_t &time){
  rejection=0; // A retry classification must use this check, never an earlier denial.
  time=now();if(!ready(c.grant))return false;
  if(!hal->awdlIbssDataMayTransmit(c.grant))return reject(28);
  if(!ringIdle(c.slot))return reject(38,c.slot);
  if(c.leaseEnd!=hal->awdlIbssExclusive.deadline)return reject(39);
  return (hal->iwm_fw_valid_tx_ant(&hal->com)&c.antenna)==c.antenna?true:reject(40);
 }
 bool prepareBuffer(const uint8_t *frame,size_t length,Buffer &b){
#ifndef AIRPORT_VTD
  return reject(30);
#else
  if(held||length>sizeof(b.storage)||!ItlAwdl::ibssDataHeaderLength(frame,length))return reject(31);
  const unsigned headerLength=ItlAwdl::ibssDataHeaderLength(frame,length);
  memcpy(b.storage,frame,length);b.frame=b.storage;b.length=length;unsigned chunks=1;
  const int error=mbuf_allocpacket(MBUF_DONTWAIT,length,&chunks,&b.m);if(error||!b.m)return reject(32,unsigned(error));held=b.m;
  if(mbuf_next(b.m)||mbuf_maxlen(b.m)<length)return reject(33);
  mbuf_setlen(b.m,length);mbuf_pkthdr_setlen(b.m,length);memcpy(mbuf_data(b.m),b.storage,length);mbuf_adj(b.m,headerLength);
  if(mbuf_len(b.m)!=length-headerLength||mbuf_pkthdr_len(b.m)!=length-headerLength)return reject(34);
  const unsigned slot=unsigned(hal->com.txq[reserved.queue].cur);if(!ringIdle(slot))return reject(35,slot);
  auto d=&hal->com.txq[reserved.queue].data[slot];IOPhysicalSegment seg[18];b.count=bus_dmamap_packet(d->map,b.m,seg,18,false);
  if(!b.count||b.count>18)return reject(36,b.count);b.commandAddress=d->cmd_paddr;b.scratchAddress=d->scratch_paddr;
  for(unsigned i=0;i<b.count;++i){if(seg[i].length>UINT32_MAX)return reject(37,i);b.segments[i]={seg[i].location,uint32_t(seg[i].length)};}
  // Verify the actual VTD bounce bytes, not only the source mbuf or descriptor lengths.
  OSSynchronizeIO();
  if(!d->map->dma.vaddr||d->map->dma.size<length-headerLength||memcmp(d->map->dma.vaddr,b.storage+headerLength,length-headerLength))return reject(41);
  // Byte comparison above remains mandatory for every frame. Only success logging is sampled.
  if(dmaEvidenceSamples<16){
   uint32_t hash=2166136261u;const auto mapped=static_cast<const uint8_t*>(d->map->dma.vaddr);
   for(size_t i=0;i<length-headerLength;++i)hash=(hash^mapped[i])*16777619u;
   ++dmaEvidenceSamples;hal->awdlIbssExperimentEvent(47,1,0,unsigned(length),hash);
  }else if(dmaEvidenceSamples==16){
   ++dmaEvidenceSamples;hal->awdlIbssExperimentEvent(47,2,0,16,0);
  }
  return true;
#endif
 }
 void discardBuffer(Buffer &b){auto m=b.m;b.m=nullptr;held=nullptr;if(m)mbuf_freem(m);}
 void stage(const ItlIbss::PeerTxContext &c,Buffer &b,const iwm_device_cmd &cmd,const iwm_tfd &tfd,uint16_t scheduler){
  auto sc=&hal->com;auto r=&sc->txq[c.grant.queue];auto d=&r->data[c.slot];
  memcpy(&r->cmd[c.slot],&cmd,sizeof(cmd));memcpy(&r->desc[c.slot],&tfd,sizeof(tfd));
  d->m=b.m;d->in=nullptr;d->totlen=int(b.length);d->fc=htole16(ItlAwdl::dataLe16(b.storage));d->txrate=d->txmcs=0;memset(&d->info,0,sizeof(d->info));
  auto table=static_cast<iwm_agn_scd_bc_tbl*>(sc->sched_dma.vaddr);const auto bc=htole16(scheduler);table[c.grant.queue].tfd_offset[c.slot]=bc;
  if(c.slot<IWM_TFD_QUEUE_SIZE_BC_DUP)table[c.grant.queue].tfd_offset[IWM_TFD_QUEUE_SIZE_MAX+c.slot]=bc;
  r->cur=(c.slot+1)%IWM_TX_RING_COUNT;++r->queued;__sync_synchronize();
 }
 void doorbell(const ItlIbss::PeerTxContext &c){IWM_WRITE(&hal->com,IWM_HBUS_TARG_WRPTR,(c.grant.queue<<8)|hal->com.txq[c.grant.queue].cur);}
 bool matching(unsigned slot){if(slot>=IWM_TX_RING_COUNT)return false;auto r=&hal->com.txq[reserved.queue];return held&&r->data[slot].m==held&&r->queued==1&&r->tail==int(slot)&&r->cur==int((slot+1)%IWM_TX_RING_COUNT);}
 void reclaim(const ItlIbss::PeerTxContext &c,Buffer &b){
  if(!matching(c.slot)){fault();return;}auto sc=&hal->com;auto r=&sc->txq[c.grant.queue];auto d=&r->data[c.slot];
  hal->iwm_reset_sched(sc,c.grant.queue,int(c.slot),c.grant.station);r->queued=0;r->tail=r->cur;b.m=nullptr;held=nullptr;hal->iwm_txd_done(sc,d);
 }
};
// Global port avoids changing ItlIwm's ABI layout. All access is on its main gate.
static ItlIbssPeerPort *itlIbssPeerPort=nullptr;
static bool itlIbssPeerNext(ItlIwm *hal,const uint8_t *frame,size_t length){
 auto p=itlIbssPeerPort;
 if(!p||p->hal!=hal||!p->onWorker()||p->retiring||p->calls||
    !frame||length>sizeof(p->request)||!ItlAwdl::ibssDataHeaderLength(frame,length)||
    memcmp(frame+4,p->reserved.peer,6)||memcmp(frame+10,p->reserved.local,6)||
    !p->queueDrained(p->reserved)||!p->data.rearmAfterAck())return false;
 memcpy(p->request,frame,length);p->requestLength=length;return true;
}
static void itlIbssPeerFinish(ItlIbssPeerPort *p){
 if(!p||!p->retiring||p->calls)return;
 p->data.hardwareStopped();p->commands.hardwareStopped();
 if(p->held||p->data.pending()||(p->peer.everUsed&&!p->peer.released))return;
 itlIbssPeerPort=nullptr;delete p;
}
bool ItlIwm::awdlIbssPeerActive(){return itlIbssPeerPort&&itlIbssPeerPort->hal==this;}
bool ItlIwm::awdlIbssPeerPrepare(const uint8_t *frame,size_t length){
 ItlAwdlRocLifetime lifetime(this,getMainCommandGate(),getMainWorkLoop());
 if(!getMainWorkLoop()->inGate())return false;
 if(getMainWorkLoop()->onThread()||dmaDetaching||itlIbssPeerPort||(awdlIbssTxPort&&!awdlIbssManagementDrained())){awdlIbssExperimentEvent(41,16,EBUSY,0,0);return false;}
 auto p=new ItlIbssPeerPort(this);if(!p){awdlIbssExperimentEvent(41,13,ENOMEM,0,0);return false;}
 if(length>sizeof(p->request)){p->reject(12,unsigned(length));delete p;return false;}
 if(!p->reserve(frame,length)){delete p;return false;}
 itlIbssApplyObservedCapabilities(this,p->reserved,iwm_mimo_enabled(&com)?2:1);
 memcpy(p->request,frame,length);
 itlIbssPeerPort=p;++p->calls;
 const int error=p->commands.create(p->reserved);const bool submitted=!error&&p->peer.ready&&!p->retiring;
 if(submitted)p->requestLength=length;
 if(error||!submitted){p->reject(error?14:15,unsigned(error));p->peer.ready=false;awdlIbssExclusive.resetRequested=true;}
 --p->calls;itlIbssPeerFinish(p);return submitted;
}
bool ItlIwm::awdlIbssPeerSubmit(){
 auto p=itlIbssPeerPort;if(!p||p->hal!=this||!getMainWorkLoop()->inGate()||getMainWorkLoop()->onThread())return false;
 ++p->calls;const bool sent=!p->retiring&&p->data.start(p->request,p->requestLength,itlNativeSearchPeerOwned(this)&&awdlNativeSearch.receiveValidation());
 if(!sent)awdlIbssExperimentEvent(45,p->data.rejectionCode(),int(p->rejection),(p->retiring?1u:0u)|(p->peer.ready?2u:0u)|(p->peer.unknown?4u:0u)|(p->peer.dataUsed?8u:0u),unsigned(p->requestLength));
 --p->calls;itlIbssPeerFinish(p);return sent;
}
bool ItlIwm::awdlIbssPeerTxComplete(unsigned queue,unsigned slot,const void *payload,size_t length){
 auto p=itlIbssPeerPort;if(!p||p->hal!=this||queue!=p->reserved.queue)return false;
 if(!getMainWorkLoop()->inGate())return true;
 // Includes duplicate/late notifications after drained peer removal.
 if(!p->data.pending())return true;
 ++p->calls;
 const auto r=static_cast<const iwm_tx_resp*>(payload);
 p->noticeSlot=slot;p->noticeLength=uint32_t(length);p->noticeCount=0;p->noticeSsn=p->noticeStatus=UINT32_MAX;
 bool valid=payload&&length>=sizeof(*r);
 if(valid)p->noticeCount=r->frame_count;
 if(valid)valid=r->frame_count==1&&length>=sizeof(*r)+sizeof(uint32_t)+sizeof(iwm_agg_tx_status);
 if(valid){p->noticeSsn=iwm_get_scd_ssn(const_cast<iwm_tx_resp*>(r));p->noticeStatus=le16toh(r->status[0].status);}
 if(valid)valid=uint32_t(com.sc_generation)==p->reserved.generation&&p->matching(slot)&&
  IWM_AGG_SSN_TO_TXQ_IDX(iwm_get_scd_ssn(const_cast<iwm_tx_resp*>(r)))==unsigned((slot+1)%IWM_TX_RING_COUNT);
 if(!valid)p->fault();
 else {
  const uint32_t raw=le16toh(r->status[0].status);
  using C=ItlIbss::PeerDataTx<ItlIbssPeerPort>::Completion;
  if(p->data.complete(uint32_t(com.sc_generation),queue,slot,raw)!=C::Reclaim)p->fault();
  else if(!itlIbssNativeComplete(this,uint32_t(com.sc_generation),queue,slot,raw))p->fault();
 }
 awdlIbssDataPortResult(false);
 --p->calls;itlIbssPeerFinish(p);return true;
}
void ItlIwm::awdlIbssPeerTick(){
 auto p=itlIbssPeerPort;if(!p||p->hal!=this||!getMainWorkLoop()->inGate()||getMainWorkLoop()->onThread())return;
 ++p->calls;p->data.tick();
 itlIbssNativeTick(this);
 // TX DMA can be reclaimed now, but firmware peer RX context must outlive the reply window.
 if(!p->retiring&&p->peer.dataUsed&&!p->data.pending()&&p->peer.ready&&
    !itlIbssPeerReplyWindowOpen(this,p->reserved.generation,p->now())){const int e=p->commands.remove();if(e&&e!=EBUSY){p->peer.unknown=true;awdlIbssExclusive.resetRequested=true;}}
 --p->calls;itlIbssPeerFinish(p);
}
void ItlIwm::awdlIbssPeerCancel(){
 auto p=itlIbssPeerPort;if(!p||p->hal!=this||!getMainWorkLoop()->inGate()||getMainWorkLoop()->onThread())return;
 ++p->calls;itlIbssNativeStop(this,false);p->data.cancel();--p->calls;itlIbssPeerFinish(p);
}
void ItlIwm::awdlIbssPeerStopped(){
 auto p=itlIbssPeerPort;if(!p||p->hal!=this||!getMainWorkLoop()->inGate())return;
 ++p->calls;p->retiring=true;p->stopEvidence=true;p->data.hardwareStopped();p->commands.hardwareStopped();itlNativeSearchPeerStopped(this);itlIbssNativeStop(this,p->stopProof(p->reserved));awdlIbssDataPortResult(true);--p->calls;itlIbssPeerFinish(p);
}
