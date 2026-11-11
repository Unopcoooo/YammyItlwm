// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "LengthRejectDiagnostic.hpp"
#include "PeerMulticastWindow.hpp"
#include "NativeTxTerminal.hpp"
bool ItlIwm::awdlNativeDiscoveryReady(){
 if(!getMainWorkLoop()||!getMainWorkLoop()->inGate()||!awdlNativeSyncRoleConfirmed()||
    ItlIbssTxPort::radioRejection(this,true)||awdlIbssDataActive()||(awdlIbssPeerActive()&&!itlNativeSearchPeerOwned(this)))return false;
 const auto &p=ItlAwdl::gDataPort;const auto &s=awdlNativeDiscovery;
 return p.owner&&!p.releasePending&&!p.nativeSession&&!p.tx.enabled&&p.tx.empty()&&
  !p.hardware.pending&&p.bsdInputAction&&!memcmp(p.registeredLocal,awdlNativeSearch.local,6)&&
  (!s.used||s.matches(reinterpret_cast<uintptr_t>(p.owner),awdlNativeSearch.epoch,uint32_t(com.sc_generation),itlAwdlRocNowNs()));
}
bool ItlIwm::awdlNativeDiscoveryOpen(OSObject *owner,uint64_t epoch){
 if(!awdlNativeDiscoveryReady()||owner!=ItlAwdl::gDataPort.owner||epoch!=awdlNativeSearch.epoch)return false;
 auto &s=awdlNativeDiscovery;const auto now=itlAwdlRocNowNs();
 if(!s.matches(reinterpret_cast<uintptr_t>(owner),epoch,uint32_t(com.sc_generation),now)||s.open)return false;
 s.open=true;awdlNativeDiscoveryPublish();return true;
}
int ItlIwm::awdlNativeDiscoveryOffer(OSObject *owner,const uint8_t *eth,size_t n){
 if(!awdlNativeDiscoveryReady()||owner!=ItlAwdl::gDataPort.owner||!awdlNativeDiscovery.open)return ENETDOWN;
 if(!awdlNativeDiscovery.acceptsTx(eth,n)){if(eth && n>=74 && ItlAwdl::nativeMdnsGroup(eth,eth+14) && ItlAwdl::mdnsMulticastPayload(eth,eth+14,n-14,true))++awdlNativeDiscovery.aliasRejected;
  ++awdlNativeDiscovery.rejected;return EOPNOTSUPP;}
 if(!awdlNativeDiscovery.offer(eth,n,itlAwdlRocNowNs()))return ENOBUFS;
 getMainCommandGate()->commandWakeup(&awdlIbssWorkPending,false);return 0;
}
void ItlIwm::awdlNativeDiscoveryStop(){
 if(!getMainWorkLoop()||!getMainWorkLoop()->inGate())return;
 auto &s=awdlNativeDiscovery;if(!s.used||s.closed)return;s.close();
 if(s.port&&s.port==awdlIbssTxPort)awdlIbssTxCancel();
 getMainCommandGate()->commandWakeup(&awdlIbssWorkPending,false);awdlNativeDiscoveryPublish();
}
void ItlIwm::awdlNativeDiscoveryReceive(const uint8_t *frame,size_t n,unsigned channel,uint32_t gp2){
 auto &s=awdlNativeDiscovery;
 if(!getMainWorkLoop()||!getMainWorkLoop()->inGate()||channel!=6||!s.open||!frame||n<2||!s.plain(ItlAwdl::dataLe16(frame)))return;
 s.count(s.EntryPlain);
 if(n<80){s.count(s.ShortFrame);return;}
 if(!awdlProbeCounters||!awdlNativeDiscoveryReady()){s.count(s.NotReady);return;}
 auto &c=*awdlProbeCounters;const auto now=itlAwdlRocNowNs();
 ItlAwdl::DataContext model;c.dataModelView(model);const ItlAwdl::Peer *peer=nullptr;
 if(model.peers)for(unsigned i=0;i<model.peers->count;++i)if(!memcmp(model.peers->peers[i].address,frame+10,6)){peer=&model.peers->peers[i];break;}
 const bool fresh=model.modelGeneration==uint32_t(com.sc_generation)&&now>=model.modelHostNs&&
  now-model.modelHostNs<=uint64_t(ItlAwdl::kPeerTimeoutUs)*1000&&peer&&peer->hasSync&&peer->hasChannels&&
  uint32_t(gp2-peer->lastSeenGp2)<=ItlAwdl::kPeerTimeoutUs;
 const unsigned before=s.rxLocalRejectCount;
 if(awdlNativeDiscovery.receive(frame,n,now,model.modelEpoch,fresh))getMainCommandGate()->commandWakeup(&awdlIbssWorkPending,false);
 if(s.rxLocalRejectCount>before&&before<2)
  ItlAwdl::lengthRejectDiagnostic(frame,n,before,[&](unsigned kind,unsigned code,uint32_t a,uint32_t b,uint32_t c){
   awdlIbssExperimentEvent(kind,code,int(a),b,c);
  });
}
void ItlIwm::awdlNativeDiscoveryTick(){
 if(!getMainWorkLoop()->inGate()||getMainWorkLoop()->onThread())return;
 auto &s=awdlNativeDiscovery;
 if(!s.used)return;
 if(!awdlNativeDiscoveryReady())awdlNativeDiscoveryStop();
 if(s.port){
  auto p=awdlIbssTxPort;
  if(!p){s.state(s.inFlightRecord,s.TransportFailed);++s.failed;s.port=nullptr;}
  else if(p!=s.port){s.close();awdlNativeSearch.fail(s.epoch,EIO);return;}
  else {
   if(p->calls||p->adapter.pending())return;
   const bool done=awdlIbssManagementDrained();
   using D=ItlIbss::ExperimentObservation;const auto &d=awdlProbeCounters->ibss;
   // A flush reply alone is not DMA proof: require the matching TX completion and reclaim.
   const bool lateSuccess=ItlAwdl::nativeTxLateSuccess(done,p->held,p->adapter.pending(),p->mismatch,
    d.get(D::TxGeneration),uint32_t(com.sc_generation),d.get(D::TxCompleted),d.get(D::TxReclaimed),
    d.get(D::TxStopReclaimed),d.get(D::TxDeadlineExpired),d.get(D::TxFlushFailed),
    d.get(D::TxCancelled),d.get(D::TxMismatch),d.get(D::TxStatus));
   const bool expired=!done&&s.open&&!s.closed&&!awdlIbssExclusive.resetRequested&&p->hal==this&&!p->retiring&&!p->mismatch&&!p->held&&
    d.get(D::TxGeneration)==uint32_t(com.sc_generation)&&d.get(D::TxCompleted)==1&&
    d.get(D::TxReclaimed)==1&&!d.get(D::TxStopReclaimed)&&d.get(D::TxDeadlineExpired)==1&&
    d.get(D::TxFlushRequested)==1&&!d.get(D::TxFlushFailed)&&!d.get(D::TxCancelled)&&
    !d.get(D::TxMismatch)&&(d.get(D::TxStatus)&255)==IWM_TX_STATUS_FAIL_FIFO_FLUSHED;
   s.state(s.inFlightRecord,(done||lateSuccess)?s.Drained:expired?s.Expired:s.TransportFailed);s.inFlightRecord=0;
   if(done||lateSuccess)++s.completed;else if(expired)++s.expiredTx;
   else{++s.failed;s.close();awdlNativeSearch.fail(s.epoch,EIO);}
   awdlIbssTxDispose();if(awdlIbssTxPort)return;s.port=nullptr;awdlNativeDiscoveryPublish();
  }
 }
 if(!s.open||s.closed||!awdlNativeDiscoveryReady())return;
 if(!itlNativeSearchPeerPrepare(this))return;
 if(s.peerFlight||!s.txCount)return;
 if(awdlIbssTxPort){s.waiting(s.Busy);return;}
 auto &t=awdlNativeControlTx;
 // Control advertisements win arbitration; there is only one queue-3 DMA owner.
 if(t.phase==ItlAwdl::NativeSearchControlTx::Pending||t.schedule.pending){s.waiting(s.Busy);return;}
 const auto before=itlAwdlRocNowNs();if(!iwm_nic_lock(&com))return;
 const auto gp2=iwm_read_prph(&com,IWM_DEVICE_SYSTEM_TIME_REG);iwm_nic_unlock(&com);const auto after=itlAwdlRocNowNs();
 if(after<before||after-before>2000000ULL||!t.epochHostNs||after<t.epochHostNs)return;
 const auto age=(after-t.epochHostNs)/1000;const uint32_t elapsed=gp2-t.epochGp2;
 if(age>ItlAwdl::nativeReceiveLimit(awdlNativeSearch.receiveValidation())/1000||elapsed>ItlAwdl::nativeReceiveLimit(awdlNativeSearch.receiveValidation())/1000||(age>elapsed?age-elapsed:elapsed-age)>2048)return;
 s.discardExpiredTx(after);if(!s.txCount)return;
 ItlAwdl::PeerMulticastWindow visit;
 ItlAwdl::DataContext model;awdlProbeCounters->dataModelView(model);
 if(model.peers && model.modelEpoch && model.modelGeneration==s.generation &&
    after>=model.modelHostNs && after-model.modelHostNs<=500000000ULL){
  const bool unicast=!(s.tx[s.txHead].bytes[0]&1);
  for(unsigned i=0;i<model.peers->count;++i)
   if(!unicast||!memcmp(model.peers->peers[i].address,s.tx[s.txHead].bytes,6))visit.consider(model.peers->peers[i],gp2);
 }
 uint64_t close=0;
 const bool unicast=!(s.tx[s.txHead].bytes[0]&1);
 const int selected=unicast?s.peerIndex(s.tx[s.txHead].bytes):-1;
 if(unicast&&(selected<0||!s.peers[selected].published||!s.peers[selected].prepared||!visit.peer)){s.waiting(s.Window);s.rotateTx();return;}
 if(visit.hasSchedule){
  // The local schedule advertises 6 in every EAW. Avoid local edges as well.
  constexpr uint32_t period=65536,guard=3072+2048;
  const uint32_t phase=elapsed%period;
  if(!visit.peer || phase<guard || phase+2000>=period-guard || s.endNs<=after){s.waiting(s.Window);s.rotateTx();return;}
  uint32_t span=period-guard-phase;
  if(visit.remaining<span)span=visit.remaining;
  const uint64_t ns=uint64_t(span)*1000;
  close=ns<s.endNs-after?after+ns:s.endNs;
  if(close-after<=2000000){s.waiting(s.Window);s.rotateTx();return;}
 }else if(!ItlAwdl::NativeDiscoverySession::window(elapsed,after,s.endNs,close)){s.waiting(s.Window);s.rotateTx();return;}
 ItlAwdl::NativeDiscoverySession::Packet packet;uint8_t id=0;
 if(!s.takeTx(after,packet,false,&id))return;
 uint8_t frame[ItlAwdl::kAwdlDataMax];size_t length=0;
 if(ItlAwdl::encodeData(packet.bytes,packet.length,s.local,s.wifiSequence,s.awdlSequence,frame,sizeof(frame),length)!=ItlAwdl::DataError::Ok){s.state(id,s.PrepareFailed);++s.rejected;return;}
 if(unicast){
  if(!itlNativeSearchPeerSend(this,frame,length,id,after,close,packet.softwareRetries))return;
  if(s.submitted<=16){s.txWindowPeer[s.submitted-1]=visit.peer;s.txWindowSlot[s.submitted-1]=uint8_t(visit.slot);
  s.txElapsed[s.submitted-1]=elapsed;}++s.peerWindowTx;awdlNativeDiscoveryPublish();return;
 }
 auto p=new ItlIbssTxPort(this,true,close);if(!p){s.state(id,s.PrepareFailed);++s.rejected;return;}
 using D=ItlIbss::ExperimentObservation;auto &d=awdlProbeCounters->ibss;
 const D::Metric metrics[]={D::TxAccepted,D::TxSubmitted,D::TxCompleted,D::TxReclaimed,D::TxStopReclaimed,
  D::TxDeadlineExpired,D::TxMismatch,D::TxCancelled,D::TxFlushRequested,D::TxFlushFailed,D::TxStatus};
 for(auto metric:metrics)d.set(metric,0);
 p->record("AWDLIbssTxVersion",6);p->record("AWDLIbssTxGeneration",s.generation);
 s.port=p;awdlIbssTxPort=p;++p->calls;const bool sent=p->adapter.start(frame,length);--p->calls;
 if(sent){s.inFlightRecord=id;s.state(id,s.Submitted);s.repliesSubmitted+=s.dnsResponse(packet.bytes,packet.length);if(s.submitted<16){s.txWindowPeer[s.submitted]=visit.peer;s.txWindowSlot[s.submitted]=uint8_t(visit.slot);s.txElapsed[s.submitted]=elapsed;}s.peerWindowTx+=visit.peer!=0;++s.submitted;s.wifiSequence=uint16_t((s.wifiSequence+1)&4095);s.awdlSequence=uint16_t((s.awdlSequence+1)&4095);}
 else{
  // rejection 28 is a channel-window miss.  It is not a transport failure:
  // the descriptor was never published.  Return the copied BSD frame to the
  // queue once for the next window instead of retiring the whole receive
  // session and restoring ordinary Wi-Fi in the middle of AirDrop discovery.
  const bool unpublishedWindowMiss=p->adapter.rejectionStage()==28&&!p->held&&!p->adapter.pending()&&!p->mismatch;
  if(unpublishedWindowMiss){
   const uint64_t current=itlAwdlRocNowNs();
   const bool queued=s.retryFront(packet.bytes,packet.length,packet.softwareRetries,id,current);
   s.state(id,queued?s.WindowMiss:s.RetryExhausted);
   if(!queued)++s.failed;
   s.port=nullptr;awdlIbssTxPort=nullptr;delete p;
   if(!queued){s.close();awdlNativeSearch.fail(s.epoch,EIO);}
  }else{
   s.state(id,s.TransportFailed);++s.failed;s.close();awdlNativeSearch.fail(s.epoch,EIO);
  }
 }
 if(p->retiring)awdlIbssTxDispose();awdlNativeDiscoveryPublish();
}
void ItlIwm::awdlNativeDiscoveryPublish(){
 if(!getMainWorkLoop()||!getMainWorkLoop()->inGate()||!getController())return;
 const auto &s=awdlNativeDiscovery;auto d=OSDictionary::withCapacity(18);if(!d)return;
 const char *keys[]={"Version","Epoch","Generation","Open","Closed","DeadlineNs","TxQueued","RxQueued","Offered","Rejected","Submitted","Completed","Failed","Received","Delivered","InputErrors","Outstanding","CABBudgetUsed","PeerWindowTx","ServiceUpdates","ServiceMifTx","RepliesSubmitted","TxTraceOverflow","AliasRejected","RxLocalSyn8770","RxLocalNd","ExpiredTx"};
 const uint64_t values[]={1,s.epoch,s.generation,s.open,s.closed,s.endNs,s.txCount,s.rxCount,s.offered,s.rejected,s.submitted,s.completed,s.failed,s.received,s.delivered,s.inputErrors,s.port&&s.port==awdlIbssTxPort&&s.port->adapter.pending(),awdlNativeControlTx.submitted+s.submitted,s.peerWindowTx,s.mifService.accepted,s.mifService.sent,s.repliesSubmitted,s.traceOverflow,s.aliasRejected,s.rxLocalSyn,s.rxLocalNd,s.expiredTx};
 bool ok=true;for(unsigned i=0;i<sizeof(values)/sizeof(values[0])&&ok;++i){auto n=OSNumber::withNumber(values[i],64);if(!n){ok=false;break;}ok=d->setObject(keys[i],n);n->release();}
 const char *peerResultKeys[]={"PeerTxResult0","PeerTxResult1","PeerTxResult2","PeerTxResult3","PeerTxResult4","PeerTxResult5","PeerTxResult6","PeerTxResult7","PeerTxResult8","PeerTxResult9","PeerTxResult10","PeerTxResult11","PeerTxResult12","PeerTxResult13","PeerTxResult14","PeerTxResult15"};
 for(unsigned i=0;i<s.peerSubmitted&&i<16&&ok;++i){
  auto v=OSNumber::withNumber(s.peerTxResults[i],64);if(!v){ok=false;break;}
  ok=d->setObject(peerResultKeys[i],v);v->release();
 }
 const char *peerKeysStatus[]={"PeerPreparing","PeerPrepared","PeerPublished","PeerOutstanding","UnicastSubmitted","UnicastCompleted","UnicastFailed","UnicastDelivered","UnicastRetryExhausted","UnicastSoftwareRetries"};
 const uint64_t peerValues[]={s.peerPreparing,s.peerPrepared,s.peerPublished,s.peerFlight,s.peerSubmitted,s.peerCompleted,s.peerFailures,s.peerDelivered,s.peerRetryExhausted,s.peerSoftwareRetries};
 for(unsigned i=0;i<sizeof(peerValues)/sizeof(peerValues[0])&&ok;++i){auto v=OSNumber::withNumber(peerValues[i],64);if(!v){ok=false;break;}ok=d->setObject(peerKeysStatus[i],v);v->release();}
 unsigned peerCount=0,preparedCount=0,publishedCount=0;
 for(const auto &peer:s.peers){peerCount+=peer.key.valid();preparedCount+=peer.prepared;publishedCount+=peer.published;}
 const char *multiKeys[]={"PeerCount","PeerPreparedCount","PeerPublishedCount","PeerCapacity","PeerFlightIndex"};
 const uint64_t multiValues[]={peerCount,preparedCount,publishedCount,s.PeerCapacity,s.peerFlight?s.flightPeer:s.PeerCapacity};
 for(unsigned i=0;i<5&&ok;++i){auto v=OSNumber::withNumber(multiValues[i],64);if(!v){ok=false;break;}ok=d->setObject(multiKeys[i],v);v->release();}
 const char *addressKeys[]={"RxAddressedLocal","RxAddressedGroup","RxAddressedForeign"};
 const uint32_t addressValues[]={s.rxAddressedLocal,s.rxAddressedGroup,s.rxAddressedForeign};
 for(unsigned i=0;i<3&&ok;++i){auto v=OSNumber::withNumber(addressValues[i],32);if(!v){ok=false;break;}ok=d->setObject(addressKeys[i],v);v->release();}
 const char *decodeKeys[]={"RxDecodeOk","RxDecodeLength","RxDecodeAddress","RxDecodeProtocol","RxDecodeLayout","RxDecodePeer","RxDecodeCapacity","RxDecodeAlias"};
 for(unsigned i=0;i<8&&ok;++i){auto v=OSNumber::withNumber(s.rxDecodeReasons[i],32);if(!v){ok=false;break;}ok=d->setObject(decodeKeys[i],v);v->release();}
 const char *rejectKeys[]={"RxLocalReject0","RxLocalReject1","RxLocalReject2","RxLocalReject3","RxLocalReject4","RxLocalReject5","RxLocalReject6","RxLocalReject7"};
 for(unsigned i=0;i<s.rxLocalRejectCount&&i<8&&ok;++i){auto v=OSNumber::withNumber(s.rxLocalReject[i],64);if(!v){ok=false;break;}ok=d->setObject(rejectKeys[i],v);v->release();}
 const char *targetKeys[]={"RxTargetIngress","RxTargetCrc","RxTargetLength","RxTargetPhy","RxTargetDecrypt","RxTargetDuplicate","RxTargetReorder","RxTargetCommon","RxTargetBuffer"};
 const char *plainKeys[]={"RxTargetPlainIngress","RxTargetPlainCrc","RxTargetPlainLength","RxTargetPlainPhy","RxTargetPlainDecrypt","RxTargetPlainDuplicate","RxTargetPlainReorder","RxTargetPlainCommon","RxTargetPlainBuffer"};
 const char *firstKeys[]={"RxTargetFirst0","RxTargetFirst1","RxTargetFirst2","RxTargetFirst3","RxTargetFirst4","RxTargetFirst5","RxTargetFirst6","RxTargetFirst7","RxTargetFirst8"};
 const char *timeKeys[]={"RxTargetFirstUs0","RxTargetFirstUs1","RxTargetFirstUs2","RxTargetFirstUs3","RxTargetFirstUs4","RxTargetFirstUs5","RxTargetFirstUs6","RxTargetFirstUs7","RxTargetFirstUs8"};
 static_assert(sizeof(targetKeys)/sizeof(*targetKeys)==ItlAwdl::RawStageCount,"raw stage schema");
 for(unsigned i=0;i<ItlAwdl::RawStageCount&&ok;++i){
  const char *names[]={targetKeys[i],plainKeys[i],firstKeys[i],timeKeys[i]};
  const uint64_t values[]={s.rxTarget[i],s.rxTargetPlain[i],s.rxTargetFirst[i],s.rxTargetFirstUs[i]};
  for(unsigned j=0;j<4&&ok;++j){auto v=OSNumber::withNumber(values[j],64);if(!v){ok=false;break;}ok=d->setObject(names[j],v);v->release();}
 }
 const char *rxKeys[]={"RxRawData","RxRawPlain","RxCommonData","RxCommonPlain","RxCommonChained","RxEntryPlain","RxShortFrame","RxNotReady","RxStalePeer","RxQueueFull","RxEnvelope","RxMdns","RxHistoryFull","RxSequence"};
 static_assert(sizeof(rxKeys)/sizeof(*rxKeys)==ItlAwdl::NativeDiscoverySession::RxStages,"RX schema");
 for(unsigned i=0;i<s.RxStages&&ok;++i){auto n=OSNumber::withNumber(s.counts[i],32);if(!n){ok=false;break;}ok=d->setObject(rxKeys[i],n);n->release();}
 const char *txKeys[]={"TxElapsedUs0","TxElapsedUs1","TxElapsedUs2","TxElapsedUs3","TxElapsedUs4","TxElapsedUs5","TxElapsedUs6","TxElapsedUs7","TxElapsedUs8","TxElapsedUs9","TxElapsedUs10","TxElapsedUs11","TxElapsedUs12","TxElapsedUs13","TxElapsedUs14","TxElapsedUs15"};
 const char *peerKeys[]={"TxWindowPeer0","TxWindowPeer1","TxWindowPeer2","TxWindowPeer3","TxWindowPeer4","TxWindowPeer5","TxWindowPeer6","TxWindowPeer7","TxWindowPeer8","TxWindowPeer9","TxWindowPeer10","TxWindowPeer11","TxWindowPeer12","TxWindowPeer13","TxWindowPeer14","TxWindowPeer15"};
 const char *slotKeys[]={"TxWindowSlot0","TxWindowSlot1","TxWindowSlot2","TxWindowSlot3","TxWindowSlot4","TxWindowSlot5","TxWindowSlot6","TxWindowSlot7","TxWindowSlot8","TxWindowSlot9","TxWindowSlot10","TxWindowSlot11","TxWindowSlot12","TxWindowSlot13","TxWindowSlot14","TxWindowSlot15"};
 for(unsigned i=0;i<s.submitted&&i<16&&ok;++i){
  const char *names[]={txKeys[i],peerKeys[i],slotKeys[i]};
  const uint32_t fields[]={s.txElapsed[i],s.txWindowPeer[i],s.txWindowSlot[i]};
  for(unsigned k=0;k<3&&ok;++k){
   auto number=OSNumber::withNumber(fields[k],32);
   if(!number){ok=false;break;}
   ok=d->setObject(names[k],number);number->release();
  }
 }
 const char *traceKeys[]={"BsdTx0","BsdTx1","BsdTx2","BsdTx3","BsdTx4","BsdTx5","BsdTx6","BsdTx7","BsdTx8","BsdTx9","BsdTx10","BsdTx11","BsdTx12","BsdTx13","BsdTx14","BsdTx15","BsdTx16","BsdTx17","BsdTx18","BsdTx19","BsdTx20","BsdTx21","BsdTx22","BsdTx23"};
 for(unsigned i=0;i<s.traceCount&&ok;++i){
  const auto &r=s.trace[i];uint64_t v=(uint64_t(r.dnsBytes)<<40)|(uint64_t(r.checksum)<<24)|(uint64_t(r.response)<<16)|(uint64_t(r.wait)<<8)|r.state;
  auto n=OSNumber::withNumber(v,64);if(!n){ok=false;break;}ok=d->setObject(traceKeys[i],n);n->release();
 }
 if(ok)getController()->setProperty("AWDLNativeDiscovery",d);d->release();
}
