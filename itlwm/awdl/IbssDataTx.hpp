// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "NativeReceivePolicy.hpp"
#include "IbssPeer.hpp"
#include "Descriptor.hpp"
namespace ItlIbss {
// Match normal IWM unicast traffic instead of the old one-retry probe setting.
constexpr uint8_t kPeerDataRetryLimit=15,kPeerRtsRetryLimit=3;
inline bool samePeerGrant(const PeerGrant &a,const PeerGrant &b){
 const auto &x=a.base;const auto &y=b.base;
 return a.valid()&&b.valid()&&a.generation==b.generation&&a.station==b.station&&a.queue==b.queue&&a.color==b.color&&
  a.capabilitiesObserved==b.capabilitiesObserved&&a.capabilityFlags==b.capabilityFlags&&
  !memcmp(a.local,b.local,6)&&!memcmp(a.peer,b.peer,6)&&x.mac==y.mac&&x.tsf==y.tsf&&x.phy==y.phy&&
  x.mcastSta==y.mcastSta&&x.bcastSta==y.bcastSta&&x.mcastQueue==y.mcastQueue&&x.bcastQueue==y.bcastQueue;
}
inline bool peerDataDescriptor(const PeerState &s,const uint8_t *frame,size_t length,
 uint64_t commandAddress,uint64_t scratchAddress,const Segment *segments,unsigned count,unsigned slot,
 uint8_t antenna,unsigned priority,iwm_device_cmd &command,iwm_tfd &tfd,uint16_t &scheduler){
 const auto headerLength=ItlAwdl::ibssDataHeaderLength(frame,length);
 if(!s.ready||s.busy||s.unknown||s.released||!s.stationMayExist||!s.queueMayExist||!s.grant.valid()||
    !headerLength||memcmp(frame+4,s.grant.peer,6)||memcmp(frame+10,s.grant.local,6)||
    (antenna!=1&&antenna!=2&&antenna!=4)||priority>3||
    !descriptorBuffersValid(length,commandAddress,scratchAddress,segments,count,slot,sizeof(command.data),headerLength))return false;
 if(ItlAwdl::dataOverlap(frame,length,reinterpret_cast<const uint8_t*>(&command),sizeof(command))||
    ItlAwdl::dataOverlap(frame,length,reinterpret_cast<const uint8_t*>(&tfd),sizeof(tfd))||
    ItlAwdl::dataOverlap(reinterpret_cast<const uint8_t*>(segments),count*sizeof(Segment),reinterpret_cast<const uint8_t*>(&command),sizeof(command))||
    ItlAwdl::dataOverlap(reinterpret_cast<const uint8_t*>(segments),count*sizeof(Segment),reinterpret_cast<const uint8_t*>(&tfd),sizeof(tfd)))return false;
 memset(&command,0,sizeof(command));memset(&tfd,0,sizeof(tfd));
 command.hdr.code=IWM_TX_CMD;command.hdr.qid=s.grant.queue;command.hdr.idx=uint8_t(slot);
 auto tx=reinterpret_cast<iwm_tx_cmd*>(command.data);
 txLe16(&tx->len,uint16_t(length));tx->sta_id=s.grant.station;tx->tid_tspec=0;
 txLe32(&tx->tx_flags,IWM_TX_CMD_FLG_SEQ_CTL|IWM_TX_CMD_FLG_ACK|(priority<<IWM_TX_CMD_FLG_BT_PRIO_POS));
 if(headerLength==26){txLe32(&tx->tx_flags,IWM_TX_CMD_FLG_ACK|(priority<<IWM_TX_CMD_FLG_BT_PRIO_POS)|IWM_TX_CMD_FLG_MH_PAD);txLe16(&tx->offload_assist,IWM_TX_CMD_OFFLD_PAD);}
 txLe32(&tx->rate_n_flags,0x0d|(uint32_t(antenna)<<14));
 // Raw experimental field value, as in the AUX data probe; units remain unproven.
 txLe32(&tx->life_time,4000);tx->data_retry_limit=kPeerDataRetryLimit;
 tx->rts_retry_limit=kPeerRtsRetryLimit;
 txLe32(&tx->dram_lsb_ptr,uint32_t(scratchAddress));tx->dram_msb_ptr=uint8_t(scratchAddress>>32);
 memcpy(command.data+sizeof(*tx),frame,headerLength);
 const unsigned tail=(sizeof(iwm_cmd_header)+sizeof(*tx)+headerLength-20+3)&~3u;
 tfd.num_tbs=uint8_t(2+count);transferBuffer(tfd.tbs[0],commandAddress,20);transferBuffer(tfd.tbs[1],commandAddress+20,tail);
 for(unsigned i=0;i<count;++i)transferBuffer(tfd.tbs[i+2],segments[i].address,segments[i].length);
 scheduler=ItlAwdl::auxUnencryptedSchedulerEntry(uint16_t(length),s.grant.station);return true;
}
struct PeerTxContext {PeerGrant grant;unsigned slot=0;uint64_t now=0,leaseEnd=0;uint8_t antenna=0;unsigned priority=0;};
// Same immutable-buffer/stage/doorbell/stop-proof contract as SingleFrameAdapter.
// The native Transport is in IbssPeerRuntime.hpp. Never release peer resources here;
// PeerCommands must separately prove drained descriptors before removing the peer.
template<class Transport> class PeerDataTx {
 Transport &t;PeerState &peer;PeerTxContext context;typename Transport::Buffer buffer={};
 ItlAwdl::AuxTxOwnership owner;bool used=false,stopped=false,preparing=false,published=false,held=false;
 bool ack=false,done=false;uint32_t status=UINT32_MAX;unsigned failure=0,reuses=0;
 bool fail(unsigned stage){if(!failure)failure=stage;return false;}
 bool validPeer()const{return peer.ready&&!peer.busy&&!peer.unknown&&!peer.released&&peer.stationMayExist&&peer.queueMayExist&&samePeerGrant(peer.grant,context.grant);}
 void discard(){if(held){held=false;t.discardBuffer(buffer);}}
 void reclaim(){if(held){held=false;t.reclaim(context,buffer);}}
 void flush(){
  if(!owner.pending||owner.flushSent)return;
  uint64_t now=t.now();
  if(t.generation()!=context.grant.generation||now>UINT64_MAX-2000000000ULL){owner.quarantine();peer.unknown=true;peer.ready=false;return;}
  if(owner.requestFlush(context.grant.generation,now+2000000000ULL)&&!peer.flushSent){
   peer.flushSent=true;
   if(!t.flush(1u<<context.grant.queue)){owner.quarantine();peer.unknown=true;peer.ready=false;}
  }
 }
public:
 enum class Completion {Unowned,Consume,Reclaim,Defer};
 PeerDataTx(Transport &transport,PeerState &state):t(transport),peer(state){}
 PeerDataTx(const PeerDataTx &)=delete;PeerDataTx &operator=(const PeerDataTx &)=delete;
 // Explicit three-packet experiment only; ordinary start remains one-shot.
 // The native caller additionally proves the actual ring drained before rearming.
private:
 bool canRecycle(bool allowLoss){
  const bool delivery=ack||(allowLoss&&(status&IWM_TX_STATUS_MSK)==0x83);
  return t.inGate()&&used&&published&&done&&delivery&&!stopped&&!preparing&&!held&&!owner.pending&&
     !owner.flushSent&&!peer.flushSent&&reuses!=UINT32_MAX&&!failure&&validPeer()&&
     t.generation()==context.grant.generation&&t.now()>=context.now&&t.now()<context.leaseEnd;
 }
 bool rearmOwned(bool limited,unsigned maximumFrames){
  if(!canRecycle(!limited)||(limited&&(maximumFrames<1||reuses>=maximumFrames-1)))return false;
  if(!owner.recycle())return false;
  ++reuses;used=published=done=ack=false;status=UINT32_MAX;peer.dataUsed=false;return true;
 }
public:
 bool rearmAfterAck(unsigned maximumFrames=3){return maximumFrames<=16&&rearmOwned(true,maximumFrames);}
 // Native session remains bounded by the original lease and matching DMA retirement.
 bool rearmWithinLease(){return rearmOwned(false,0);}
 // A reclaimed LONG_LIMIT frame may cross an AWDL window boundary exactly once per retry.
 bool rearmAfterLoss(){
  if(!done||held||owner.pending||!owner.recycle())return false;
  ++reuses;used=published=done=ack=false;status=UINT32_MAX;peer.dataUsed=false;return true;
 }
 // 0x83 is firmware LONG_LIMIT; delivery failed but the matching DMA completion reclaimed the buffer.
 bool retryExhaustedReclaimed(){return !ack&&(status&IWM_TX_STATUS_MSK)==0x83&&canRecycle(true);}
 // Only the native caller may classify an elapsed window; this proves no DMA publication occurred.
 bool discardUnpublishedWindowMiss(){
  const uint64_t now=t.now();
  if(!t.inGate()||!used||(failure!=2&&failure!=4&&failure!=6)||stopped||preparing||published||held||done||
     owner.attempted||owner.pending||owner.flushSent||owner.phase!=ItlAwdl::AuxTxPhase::Ready||
     peer.flushSent||!validPeer()||t.generation()!=context.grant.generation||
     now<context.now||now>=context.leaseEnd)return false;
  used=false;failure=0;peer.dataUsed=false;return true;
 }
 bool start(const uint8_t *frame,size_t length,bool receiveValidation=false){
  if(!t.inGate()||used||peer.dataUsed||stopped||!frame||length<80||length>ItlAwdl::kAwdlDataMax)return fail(1);
  used=true;
  if(!t.snapshot(context)||!t.inGate()||peer.dataUsed||!validPeer()||context.slot>=256||context.leaseEnd<=context.now||
     context.leaseEnd-context.now>ItlAwdl::nativeReceiveLimit(receiveValidation)||t.generation()!=context.grant.generation)return fail(2);
  peer.dataUsed=true;preparing=held=true;const bool prepared=t.prepareBuffer(frame,length,buffer);preparing=false;
  uint64_t now=0;
  if(!prepared){discard();return fail(3);}
  if(stopped||!validPeer()||!t.revalidate(context,now)||!t.inGate()||stopped||!validPeer()||t.generation()!=context.grant.generation||now<context.now||now>=context.leaseEnd){discard();return fail(4);}
  iwm_device_cmd command;iwm_tfd tfd;uint16_t scheduler;
  if(buffer.length!=length||!peerDataDescriptor(peer,buffer.frame,buffer.length,buffer.commandAddress,buffer.scratchAddress,
      buffer.segments,buffer.count,context.slot,context.antenna,context.priority,command,tfd,scheduler)){discard();return fail(5);}
  const uint64_t prior=now;
  if(stopped||!validPeer()||!t.revalidate(context,now)||!t.inGate()||stopped||!validPeer()||t.generation()!=context.grant.generation||now<prior||now>=context.leaseEnd){discard();return fail(6);}
  const uint64_t left=context.leaseEnd-now,deadline=now+(left<2000000000ULL?left:2000000000ULL);
  if(!owner.attempt()||!owner.submit(context.grant.generation,context.grant.queue,context.slot,deadline)){discard();return fail(7);}
  published=true;t.stage(context,buffer,command,tfd,scheduler);t.doorbell(context);return true;
 }
 Completion complete(uint32_t gen,unsigned queue,unsigned slot,uint32_t raw){
  if(!t.inGate())return Completion::Defer;
  if(!published||queue!=context.grant.queue||slot!=context.slot)return Completion::Unowned;
  if(!owner.complete(gen,queue,slot))return Completion::Consume;
  status=raw;ack=(raw&IWM_TX_STATUS_MSK)==IWM_TX_STATUS_SUCCESS;done=true;reclaim();return Completion::Reclaim;
 }
 void cancel(){if(!t.inGate())return;stopped=true;if(!preparing)flush();}
 void tick(){
  if(!t.inGate()||preparing||!owner.pending)return;
  if(t.generation()!=context.grant.generation||t.now()>=context.leaseEnd||t.now()>owner.deadlineNs||!validPeer()){stopped=true;flush();}
  if(owner.flushSent&&t.now()>owner.deadlineNs)owner.quarantine();
 }
 bool hardwareStopped(){
  if(!t.inGate()||preparing||!published||!owner.pending||!t.stopProof(context))return false;
  stopped=true;owner.deviceQuiesced();reclaim();return true;
 }
 unsigned rejectionCode()const{return failure;}
 bool publishedToHardware()const{return published;}
 bool pending()const{return owner.pending;}bool completed()const{return done;}
 bool ackReported()const{return ack;}uint32_t rawStatus()const{return status;}
};
}
