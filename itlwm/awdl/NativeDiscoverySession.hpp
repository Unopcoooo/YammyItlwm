// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "NativeReceivePolicy.hpp"
#include "MdnsMulticast.hpp"
#include "NativeMifService.hpp"
#include "NativeSyncProfile.hpp"
#include "RawRxMetadata.hpp"
#include "NativePeerRegistration.hpp"
#include "NativePeerInventory.hpp"
#include "PeerTable.hpp"
struct ItlIbssTxPort;
namespace ItlAwdl {
// All methods are serialized by the HAL gate. The native-control job pins owner.
struct NativeDiscoverySession {
 NativeMifService mifService;
 struct PeerState {NativePeerKey key;bool preparing=false,prepared=false,published=false;};
 static constexpr unsigned PeerCapacity=NativePeerInventory::Capacity;
 PeerState peers[PeerCapacity];NativePeerInventory inventory;
 bool peerPreparing=false,peerPrepared=false,peerPublished=false,peerFlight=false;
 unsigned flightPeer=PeerCapacity;
 void updatePeers(){peerPreparing=peerPrepared=peerPublished=false;for(const auto &p:peers){peerPreparing|=p.preparing;peerPrepared|=p.prepared;peerPublished|=p.published;}}
 int peerIndex(const uint8_t *mac)const{for(unsigned i=0;i<PeerCapacity;++i)if(peers[i].key.valid()&&!memcmp(peers[i].key.mac,mac,6))return int(i);return -1;}
 uint64_t peerWindowEnd=0;
 uint32_t peerSubmitted=0,peerCompleted=0,peerFailures=0,peerDelivered=0,peerRetryExhausted=0,peerSoftwareRetries=0;
 uint8_t peerRecord=0;
 bool localUnicast(const uint8_t *eth,size_t n,bool outgoing)const{
  if(!eth||n<54||n>kEtherDataMax||dataBe16(eth+12)!=0x86dd||!dataIpv6(eth+14,n-14))return false;
  const auto src=eth+6,dst=eth;
  return !memcmp(outgoing?src:dst,local,6)&&nativePeerIdentity(src,eth+22)&&
   nativePeerIdentity(dst,eth+38)&&memcmp(src,dst,6);
 }
 bool peerPacket(const uint8_t *eth,size_t n,bool outgoing)const{
  if(!localUnicast(eth,n,outgoing))return false;
  const int i=peerIndex(outgoing?eth:eth+6);
  return i>=0&&!memcmp(outgoing?eth+38:eth+22,peers[i].key.ip,16);
 }
 // A system-visible AirDrop service needs a link-layer peer mapping before it
 // can originate a unicast connection. Learn that mapping from an already
 // validated AWDL sync advertisement instead of requiring the peer to send a
 // unicast packet to us first.
 bool learnPeer(const Peer &peer,uint32_t gen,uint32_t model,uint32_t nowGp2){
  if(!open||closed||!gen||!model||gen!=generation||!peer.hasSync||!peer.hasChannels||
     nowGp2-peer.lastSeenGp2>kPeerTimeoutUs)return false;
  bool visits=false;
  for(unsigned i=0;i<peer.channels.count;++i)visits|=peer.channels.entries[i].channel==6;
  if(!visits)return false;
  NativePeerKey key;
  memcpy(key.mac,peer.address,6);key.generation=gen;key.epoch=model;
  if(!nativePeerIpv6FromMac(key.mac,key.ip))return false;
  const int existing=peerIndex(key.mac);
  if(existing>=0){
   auto &old=peers[existing].key;
   if(memcmp(old.ip,key.ip,16))return false;
   old.generation=gen;old.epoch=model;
   inventory.refreshEpoch(key);
   return true;
  }
  for(auto &entry:peers)if(!entry.key.valid()){
   entry.key=key;entry.preparing=entry.prepared=entry.published=false;
   return true;
  }
  return false;
 }
 bool acceptsTx(const uint8_t *eth,size_t n)const{
  if(mdnsMulticastEthernet(eth,n,local))return true;
  if(!peerPacket(eth,n,true))return false;
  const auto &p=peers[peerIndex(eth)];return p.prepared&&p.published;
 }
 struct Packet {uint8_t bytes[kEtherDataMax]={};size_t length=0;uint64_t at=0;uint32_t model=0;uint8_t softwareRetries=0;};
 struct Seen {
  uint8_t mac[6]={};bool valid=false;
  uint16_t sequence[2][4]={};bool received[2][4]={};
  bool accept(uint16_t word,bool multicast,bool removeDuplicates){
   const unsigned domain=multicast?1:0,ac=(word>>12)&3;
   const uint16_t next=word&4095;
   // Native awdlLllcInput rejects only delta zero when duplicate removal is enabled.
   // Its bitmap shifts/reset have no other admission effect in this path.
   if(received[domain][ac]&&sequence[domain][ac]==next&&removeDuplicates)return false;
   sequence[domain][ac]=next;received[domain][ac]=true;return true;
  }
 };
 bool removeDuplicatesRx=false;
 uint32_t expiredTx=0;
 uint64_t peerTxResults[16]={};
 uintptr_t owner=0;uint64_t epoch=0,startNs=0,endNs=0;uint32_t generation=0;
 uint8_t local[6]={};bool used=false,open=false,closed=false;
 // Match the existing 16-frame BSD drain budget without retaining RX DMA buffers.
 static constexpr unsigned RxCapacity=16;
 // Keep enough host packets for the peer's next channel visit.  The earlier
 // four-frame value was only an experiment default; Owly's working driver
 // allows 128 queued unicast frames.  One extra slot stays reserved so a
 // failed in-flight frame can return even when the ordinary queue is full.
 static constexpr unsigned MaxQueued=128,QueueCapacity=MaxQueued+1;
 static constexpr uint64_t QueuedLifetimeNs=2000000000ULL;
// Bounded requeue attempts per copied BSD frame.
static constexpr uint8_t MaxSoftwareRetries=8;
 Packet tx[QueueCapacity],rx[RxCapacity];unsigned txHead=0,txCount=0,rxHead=0,rxCount=0;
 uint32_t offered=0,rejected=0,submitted=0,completed=0,failed=0,received=0,delivered=0,inputErrors=0;
 uint32_t peerWindowTx=0,txWindowPeer[16]={};
 uint8_t txWindowSlot[16]={};
 uint32_t modelEpoch=0;Seen seen[16];uint16_t wifiSequence=0,awdlSequence=0;
 // Per-epoch trace of copied BSD packets; no payload or kernel pointers exported.
 enum TxState:uint8_t {Queued=1,Submitted,Drained,Expired,ClosedQueued,NoCredit,PrepareFailed,TransportFailed,QueueFullAtOffer,RetryExhausted,WindowMiss};
 enum TxWait:uint8_t {Budget=1,Busy=2,Window=4};
 struct TxRecord {uint16_t dnsBytes=0,checksum=0;uint8_t response=0,state=0,wait=0;};
 TxRecord trace[24];unsigned traceCount=0,traceOverflow=0;
 uint8_t queueRecords[QueueCapacity]={},inFlightRecord=0;
 // Bounded metadata only; no packet payload or kernel addresses.
 uint32_t rxAddressedLocal=0,rxAddressedGroup=0,rxAddressedForeign=0;
 uint32_t rxDecodeReasons[8]={};uint64_t rxLocalReject[8]={};unsigned rxLocalRejectCount=0;
 // Per-stage local-destination observations survive detail-trace truncation.
 uint32_t rxTarget[RawStageCount]={},rxTargetPlain[RawStageCount]={},rxTargetFirstUs[RawStageCount]={};
 uint64_t rxTargetFirst[RawStageCount]={};
 void observeRaw(unsigned stage,const RawRxMetadata &m,uint32_t gen,uint64_t now){
  if(!used||!open||closed||gen!=generation||now<startNs||now>=endNs||
     stage>=RawStageCount||m.channel!=6||!m.lengthValid||!m.fcValid||
     !m.addressValid||memcmp(m.receiver,local,6))return;
  if(!rxTarget[stage]){
   rxTargetFirst[stage]=uint64_t(m.frameControl&0xffff)|(uint64_t(m.length&0xffff)<<16)|
      (uint64_t(m.statusValid?m.status:0)<<32);
   const uint64_t us=(now-startNs)/1000;
   rxTargetFirstUs[stage]=us>UINT32_MAX?UINT32_MAX:uint32_t(us);
  }
  if(rxTarget[stage]!=UINT32_MAX)++rxTarget[stage];
  if(plain(uint16_t(m.frameControl))&&rxTargetPlain[stage]!=UINT32_MAX)++rxTargetPlain[stage];
 }
 uint32_t repliesSubmitted=0,aliasRejected=0,rxLocalSyn=0,rxLocalNd=0;
 static bool dnsResponse(const uint8_t *eth,size_t n){return eth&&n>=74 && eth[20]==17 && dataBe16(eth+56)==5353 && (eth[64]&0x80);}
 void state(uint8_t id,TxState st){if(id && id<=traceCount)trace[id-1].state=st;}
 void waiting(TxWait why){for(unsigned i=0;i<txCount;i++){auto id=queueRecords[(txHead+i)%QueueCapacity];if(id)trace[id-1].wait|=why;}}
 void observeLocalTransport(const uint8_t *eth,size_t n){
  if(n<14+40)return;
  const uint8_t *ip=eth+14;
  const uint8_t destination[]={0xfe,0x80,0,0,0,0,0,0,uint8_t(local[0]^2),local[1],local[2],0xff,0xfe,local[3],local[4],local[5]};
  if(ip[6]==6 && n>=74 && !memcmp(ip+24,destination,16) && dataBe16(ip+42)==8770 &&
     ip[52]>>4>=5 && size_t(ip[52]>>4)*4<=n-54 && (ip[53]&0x12)==2)++rxLocalSyn;
  if(ip[6]==58 && ip[7]==255 && n>=78 && (ip[40]==135||ip[40]==136) && !ip[41] && !memcmp(ip+48,destination,16))++rxLocalNd;
 }
 // Per-open channel-six data counters, independent of the truncated detail trace.
 enum RxStage {RawData,RawPlain,CommonData,CommonPlain,CommonChained,EntryPlain,ShortFrame,NotReady,StalePeer,QueueFull,Envelope,Mdns,HistoryFull,Sequence,RxStages};
 uint32_t counts[RxStages]={},txElapsed[16]={};
 void count(RxStage k){if(counts[k]!=UINT32_MAX)++counts[k];}
 static bool plain(uint16_t fc){return ((fc&255)==8||(fc&255)==0x88)&&!(fc&0xc700);}
 void observe(bool raw,uint16_t fc,bool whole=true){
  if(!open||closed||(fc&15)!=8)return;
  count(raw?RawData:CommonData);
  if(plain(fc)){count(raw?RawPlain:CommonPlain);if(!raw&&!whole)count(CommonChained);}
 }
 ItlIbssTxPort *port=nullptr;
 bool controlRetired=false;
 bool canRearmData(uint64_t previousEpoch)const{
  return used && epoch==previousEpoch && closed && !open &&
         !port && !peerPreparing && !peerPrepared && !peerPublished && !peerFlight && !peerFailures &&
         !txCount && !rxCount && !failed && !inputErrors;
 }
 bool canRearm(uint64_t previousEpoch)const{
  return canRearmData(previousEpoch)&&controlRetired;
 }
 void resetAfterRetirement(){
  // All members are scalars or inline packet buffers; no destructor, resource, or kernel stack temporary.
  static_assert(__is_trivially_copyable(NativeDiscoverySession),"revisit ownership before resetting");
  const auto nextWifi=wifiSequence, nextAwdl=awdlSequence;
  memset(this,0,sizeof(*this));
  wifiSequence=nextWifi;
  awdlSequence=nextAwdl;
 }
 bool begin(uintptr_t object,uint64_t searchEpoch,uint32_t gen,const uint8_t *address,uint64_t now,uint64_t end,bool receiveValidation=false){
  if(used||!object||!searchEpoch||!gen||!dataUnicast(address)||!now||end<=now||end-now>nativeReceiveLimit(receiveValidation))return false;
  owner=object;epoch=searchEpoch;generation=gen;startNs=now;endNs=end;memcpy(local,address,6);used=true;return true;
 }
 bool matches(uintptr_t object,uint64_t searchEpoch,uint32_t gen,uint64_t now)const{
  return used&&!closed&&owner==object&&epoch==searchEpoch&&generation==gen&&now>=startNs&&now<endNs;
 }
 void close(){
  for(unsigned i=0;i<txCount;i++)state(queueRecords[(txHead+i)%QueueCapacity],ClosedQueued);
  open=false;closed=true;txCount=rxCount=0;inventory.close();
 }
 bool offer(const uint8_t *eth,size_t n,uint64_t now){
  ++offered;
  if(!open||closed||now<startNs||now>=endNs||!acceptsTx(eth,n)){++rejected;return false;}
  const bool response=dnsResponse(eth,n);
  uint8_t id=0;
  if(traceCount<24){
   auto &r=trace[traceCount++];id=uint8_t(traceCount);
   r.dnsBytes=uint16_t(n-62);r.checksum=dataBe16(eth+60);r.response=response;r.state=Queued;
  }else ++traceOverflow;
  if(txCount==MaxQueued){state(id,QueueFullAtOffer);++rejected;return false;}
  const auto slot=(txHead+txCount)%QueueCapacity;queueRecords[slot]=id;
  auto &p=tx[slot];memcpy(p.bytes,eth,n);p.length=n;p.at=now;p.softwareRetries=0;++txCount;return true;
 }
 // Reinsert a failed frame at the head so the next AWDL window preserves flow order.
 bool retryFront(const uint8_t *eth,size_t n,uint8_t retries,uint8_t id,uint64_t now){
  if(!open||closed||now>=endNs||!eth||n<54||n>kEtherDataMax||retries>=MaxSoftwareRetries||txCount==QueueCapacity)return false;
  const auto slot=(txHead+QueueCapacity-1)%QueueCapacity;auto &p=tx[slot];
  memcpy(p.bytes,eth,n);p.length=n;p.at=now;p.softwareRetries=uint8_t(retries+1);
  queueRecords[slot]=id;txHead=slot;++txCount;++peerSoftwareRetries;state(id,Queued);return true;
 }
 // Remove stale heads before choosing a DMA owner or a peer window.
 void discardExpiredTx(uint64_t now){
  while(txCount){const auto &p=tx[txHead];
   if(open&&!closed&&now>=startNs&&now>=p.at&&now-p.at<=QueuedLifetimeNs&&now<endNs)return;
   state(queueRecords[txHead],Expired);++rejected;txHead=(txHead+1)%QueueCapacity;--txCount;
  }
 }
 bool takeTx(uint64_t now,Packet &out,bool replyOnly=false,uint8_t *record=nullptr){
  while(txCount){
   discardExpiredTx(now);if(!txCount)break;auto &p=tx[txHead];
   const auto id=queueRecords[txHead];txHead=(txHead+1)%QueueCapacity;--txCount;
   if(replyOnly&&!dnsResponse(p.bytes,p.length)){state(id,NoCredit);++rejected;continue;}
   out=p;if(record)*record=id;return true;}
  return false;
 }
 bool receive(const uint8_t *frame,size_t n,uint64_t now,uint32_t model,bool peerFresh){
  if(!open||closed||now<startNs||now>=endNs||!frame||n<80)return false;
  const bool addressedLocal=!memcmp(frame+4,local,6);
  if(addressedLocal)++rxAddressedLocal;else if(frame[4]&1)++rxAddressedGroup;else ++rxAddressedForeign;
  if(!peerFresh||!model){count(StalePeer);return false;}
  if(rxCount==RxCapacity){count(QueueFull);return false;}
  Packet packet;uint16_t seq=0;
  const auto decode=decodeData(frame,n,local,frame+10,packet.bytes,sizeof(packet.bytes),packet.length,seq,true);
  if(unsigned(decode)<8)++rxDecodeReasons[unsigned(decode)];
  if(decode!=DataError::Ok){
   if(addressedLocal&&rxLocalRejectCount<8){
    const uint16_t fc=dataLe16(frame),qos=(fc&0x80)?dataLe16(frame+24):0;
    rxLocalReject[rxLocalRejectCount++]=uint64_t(fc)|(uint64_t(qos)<<16)|(uint64_t(unsigned(decode))<<32)|(uint64_t(n)<<40);
   }
   count(Envelope);return false;
  }
  observeLocalTransport(packet.bytes,packet.length);
  const bool unicast=localUnicast(packet.bytes,packet.length,false);
  if(!mdnsMulticastPayload(packet.bytes,packet.bytes+14,packet.length-14,true)&&!unicast){count(Mdns);return false;}
  int peerSlot=unicast?peerIndex(packet.bytes+6):-1;
  if(unicast&&peerSlot>=0&&(!peerPacket(packet.bytes,packet.length,false)||peers[peerSlot].key.epoch!=model)){count(StalePeer);return false;}
  if(unicast&&peerSlot<0){for(unsigned i=0;i<PeerCapacity;++i)if(!peers[i].key.valid()){peerSlot=int(i);break;}
   if(peerSlot<0){count(StalePeer);return false;}}
  if(modelEpoch!=model){modelEpoch=model;rxCount=rxHead=0;for(auto &s:seen)s=Seen();}
  Seen *history=nullptr;for(auto &s:seen)if(s.valid&&!memcmp(s.mac,frame+10,6)){history=&s;break;}
  if(!history)for(auto &s:seen)if(!s.valid){history=&s;break;}
  if(!history){count(HistoryFull);return false;}
  if(!history->accept(seq,!unicast,removeDuplicatesRx)){count(Sequence);return false;}
  memcpy(history->mac,frame+10,6);history->valid=true;
  if(unicast&&!peers[peerSlot].key.valid()){
   auto &key=peers[peerSlot].key;memcpy(key.mac,packet.bytes+6,6);memcpy(key.ip,packet.bytes+22,16);
   key.generation=generation;key.epoch=model;
  }
  packet.at=now;packet.model=model;rx[(rxHead+rxCount)%RxCapacity]=packet;++rxCount;++received;return true;
 }
 void rotateTx(){
  if(txCount<2)return;
  uint8_t blocked[6];memcpy(blocked,tx[txHead].bytes,6);
  // Move other destinations ahead without reversing packets for either peer.
  // In particular, a closed window must not reorder one TCP stream's ACKs.
  for(unsigned i=0;i<txCount;++i){
   unsigned j=i;while(j<txCount&&!memcmp(tx[(txHead+j)%QueueCapacity].bytes,blocked,6))++j;
   if(j==txCount)break;
   if(j==i)continue;
   const auto packet=tx[(txHead+j)%QueueCapacity];const auto record=queueRecords[(txHead+j)%QueueCapacity];
   for(unsigned k=j;k>i;--k){tx[(txHead+k)%QueueCapacity]=tx[(txHead+k-1)%QueueCapacity];queueRecords[(txHead+k)%QueueCapacity]=queueRecords[(txHead+k-1)%QueueCapacity];}
   tx[(txHead+i)%QueueCapacity]=packet;queueRecords[(txHead+i)%QueueCapacity]=record;
  }
 }
 bool takeRx(uint64_t now,uint32_t model,Packet &out){
  unsigned remaining=rxCount;
  while(remaining--&&rxCount){auto &p=rx[rxHead];const bool fresh=open&&!closed&&now>=startNs&&now<endNs&&now>=p.at&&now-p.at<=500000000ULL&&p.model==model;
   const int i=fresh&&!(p.bytes[0]&1)?peerIndex(p.bytes+6):-1;
   if(fresh&&!(p.bytes[0]&1)&&(i<0||!peers[i].prepared||!peers[i].published)){
    const auto held=p;rxHead=(rxHead+1)%RxCapacity;rx[(rxHead+rxCount-1)%RxCapacity]=held;continue;
   }
   rxHead=(rxHead+1)%RxCapacity;--rxCount;if(fresh){out=p;return true;}}
  return false;
 }
 // Local advertised 16-EAW clock; shared multicast visits 0/10, 16 TU guards.
 // The host/GP2 residual is bounded by the caller; guard includes 2 ms uncertainty.
 static bool window(uint32_t elapsedUs,uint64_t now,uint64_t end,uint64_t &close){
  const uint32_t period=NativeSyncProfile::periodUs,slot=(elapsedUs/period)%16,phase=elapsedUs%period;
  constexpr uint32_t guard=16*1024+2048,lifetime=2000;
  if((slot!=0&&slot!=10)||phase<guard||phase+lifetime>=period-guard||end<=now)return false;
  const uint64_t remaining=uint64_t(period-guard-phase)*1000;
  close=remaining<end-now?now+remaining:end;return close>now&&close-now>uint64_t(lifetime)*1000;
 }
};
}
