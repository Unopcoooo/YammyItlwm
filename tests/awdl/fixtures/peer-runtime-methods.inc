static bool itlIbssPeerReplyWindowOpen(ItlIwm *hal,uint32_t generation,uint64_t now){
 const auto &t=itlIbssDataTrial;
 if(t.nativeQueueMode)return itlIbssNativeRetainPeer(hal,generation,now);
 return itlIbssDataOwner==hal&&t.generation==generation&&
  (t.inReplyWindow(now)||(t.smallBatch.enabled&&t.smallBatch.endNs&&now<t.smallBatch.endNs&&
    !t.cancelled&&!t.endRequested&&t.phase>=ItlIbss::DataTrialPhase::WaitingWindow&&t.phase<=ItlIbss::DataTrialPhase::Submitted));
}
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
 ++p->calls;p->retiring=true;p->stopEvidence=true;p->data.hardwareStopped();p->commands.hardwareStopped();itlIbssNativeStop(this,p->stopProof(p->reserved));awdlIbssDataPortResult(true);--p->calls;itlIbssPeerFinish(p);
}