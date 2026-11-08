// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "IbssLifecycle.hpp"
#include "DataFrames.hpp"
#include <sys/errno.h>
// Requires actual iwm_add_sta_cmd/iwm_rm_sta_cmd and STA constants before inclusion.
namespace ItlIbss {
// Reservations require the native fresh-exclusive inventory; this command core
// neither allocates resources nor grants permission to operate the radio.
struct PeerGrant {
 Reservation base={};uint32_t generation=0;uint8_t station=0,queue=0,color=0;
 uint8_t local[6]={},peer[6]={};bool independentlyReserved=false;
 uint32_t capabilityFlags=0;bool capabilitiesObserved=false;
 bool valid()const{
  return independentlyReserved&&base.valid()&&generation&&station>4&&station<16&&
   station!=base.mcastSta&&station!=base.bcastSta&&queue>=10&&queue<=31&&
   ItlAwdl::dataUnicast(local)&&ItlAwdl::dataUnicast(peer)&&memcmp(local,peer,6);
 }
};
inline void peerWord16(void *p,uint16_t v){auto b=static_cast<uint8_t*>(p);b[0]=uint8_t(v);b[1]=uint8_t(v>>8);}
inline void peerWord32(void *p,uint32_t v){auto b=static_cast<uint8_t*>(p);for(unsigned i=0;i<4;++i)b[i]=uint8_t(v>>(8*i));}
inline bool peerStationCommand(const PeerGrant &g,bool attachQueue,iwm_add_sta_cmd &out){
 if(!g.valid())return false;
 iwm_add_sta_cmd c={};c.sta_id=g.station;c.station_type=IWM_STA_LINK;
 c.add_modify=attachQueue?1:0;c.modify_mask=attachQueue?IWM_STA_MODIFY_QUEUES:0;
 peerWord16(&c.tid_disable_tx,0xffff); // no aggregation or key setup
 peerWord32(&c.mac_id_n_color,uint32_t(g.base.mac)|(uint32_t(g.color)<<8));
 memcpy(c.addr,g.peer,6);
 peerWord32(&c.station_flags_msk,IWM_STA_FLG_FAT_EN_MSK|IWM_STA_FLG_MIMO_EN_MSK|IWM_STA_FLG_RTS_MIMO_PROT);
 if(g.capabilitiesObserved){
  const uint32_t allowed=(1u<<28)|(1u<<17)|(3u<<19)|(7u<<23);
  if(g.capabilityFlags&~allowed)return false;
  peerWord32(&c.station_flags,g.capabilityFlags);
  peerWord32(&c.station_flags_msk,IWM_STA_FLG_FAT_EN_MSK|IWM_STA_FLG_MIMO_EN_MSK|IWM_STA_FLG_RTS_MIMO_PROT|(15u<<19)|(7u<<23));
 }
 // Legacy OFDM/SISO/HT20 subset; no invented peer capabilities.
 if(attachQueue)peerWord32(&c.tfd_queue_msk,1u<<g.queue);
 out=c;return true;
}
struct PeerState {
 PeerGrant grant;bool everUsed=false,busy=false,stationMayExist=false,queueMayExist=false;
 bool ready=false,unknown=false,drainMayBeSet=false,flushSent=false,released=false,dataUsed=false;
};
// Transport must prove legacy DQA/STA_TYPE API, exclusive IBSS context, actual
// MAC/color/local address and ledger/generation/main gate on every call. Its drain
// operates on this station; queueDrained is real descriptor/notification ownership
// proof, not flush ACK. stopProof must cover the ORIGINAL grant and actual DMA stop.
template<class Transport> class PeerCommands {
 Transport &t;PeerState &s;
 struct Busy {bool &v;explicit Busy(bool &b):v(b){v=true;}~Busy(){v=false;}};
 int checked(int error){if(!error&&!t.ready(s.grant))error=EIO;if(error)s.unknown=true;return error;}
 int station(bool update){
  iwm_add_sta_cmd command;if(!peerStationCommand(s.grant,update,command))return EINVAL;
  uint32_t status=1;int e=t.status(0x18,&command,sizeof(command),status);
  if(!e&&(status&255)!=1)e=EIO;return checked(e);
 }
public:
 PeerCommands(Transport &transport,PeerState &state):t(transport),s(state){}
 int create(const PeerGrant &g){
  if(s.busy)return EBUSY;
  if(!g.valid())return EINVAL;
  if(s.everUsed||!t.ready(g))return EBUSY;
  Busy lock(s.busy);s.grant=g;s.everUsed=true;
  s.stationMayExist=true;int e=station(false);if(e)return e;
  s.queueMayExist=true;e=checked(t.enableQueue(g.station,g.queue,1,0));if(e)return e;
  e=station(true);if(e)return e;s.ready=true;return 0;
 }
 int remove(){
  if(s.busy)return EBUSY;
  if(!s.everUsed||s.released)return EINVAL;
  if(s.unknown)return EIO;
  if(!t.ready(s.grant))return EBUSY;
  Busy lock(s.busy);s.ready=false;
  if(!s.drainMayBeSet){s.drainMayBeSet=true;int e=checked(t.drain(s.grant,true));if(e)return e;}
  if(!s.flushSent){s.flushSent=true;int e=checked(t.flush(1u<<s.grant.queue));if(e)return e;}
  if(!t.queueDrained(s.grant))return EBUSY; // retain peer and queue, retry after completion
  int e=checked(t.drain(s.grant,false));if(e)return e;s.drainMayBeSet=false;
  if(s.queueMayExist){e=checked(t.disableQueue(s.grant.station,s.grant.queue,0));if(e)return e;s.queueMayExist=false;}
  if(s.stationMayExist){iwm_rm_sta_cmd c={};c.sta_id=s.grant.station;e=checked(t.pdu(0x19,&c,sizeof(c)));if(e)return e;s.stationMayExist=false;}
  t.release(s.grant);s.released=true;return 0;
 }
 bool hardwareStopped(){
  if(s.busy||!s.everUsed||s.released||!t.stopProof(s.grant))return false;
  Busy lock(s.busy);s.ready=false;s.stationMayExist=s.queueMayExist=s.drainMayBeSet=false;
  t.release(s.grant);s.released=true;return true;
 }
};
}
