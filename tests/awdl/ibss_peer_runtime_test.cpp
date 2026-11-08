#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#define ITL_IBSS_DATA_TEST_MAIN data_core_original_main
#include "ibss_data_tx_test.cpp"
#undef ITL_IBSS_DATA_TEST_MAIN
#pragma clang diagnostic pop
#include "../../itlwm/awdl/IbssDataTrial.hpp"
#include "peer-response-contract.inc"
#define le16toh(x) (x)
#define IWM_TX_RING_COUNT 256
#define IWM_AGG_SSN_TO_TXQ_IDX(x) ((x)&255)
static uint32_t iwm_get_scd_ssn(iwm_tx_resp *r){uint32_t v;memcpy(&v,reinterpret_cast<uint8_t*>(r->status)+4*r->frame_count,4);return v&4095;}
struct Loop {bool gate=true,work=false;bool inGate(){return gate;}bool onThread(){return work;}};
struct ItlIwm {
 struct{unsigned sc_generation=7;}com;struct{bool resetRequested=false;}awdlIbssExclusive;Loop loop;
 Loop *getMainWorkLoop(){return &loop;}
 bool awdlIbssPeerTxComplete(unsigned,unsigned,const void*,size_t);
 void awdlIbssDataPortResult(bool){}
 void awdlIbssPeerTick();void awdlIbssPeerCancel();void awdlIbssPeerStopped();
};
static unsigned deleted=0;
struct ItlIbssPeerPort:DataTransport {
 uint8_t request[ItlAwdl::kAwdlDataMax]={};size_t requestLength=0;
 ItlIwm *hal;PeerState state;Transport pt;PeerGrant reserved;
 PeerCommands<Transport> commands;PeerDataTx<ItlIbssPeerPort> data;
 uint32_t noticeSlot=0,noticeLength=0,noticeCount=0,noticeSsn=UINT32_MAX,noticeStatus=UINT32_MAX;
 unsigned calls=0;bool retiring=false,stopEvidence=false,mismatch=false,match=true;void *held=nullptr;
 explicit ItlIbssPeerPort(ItlIwm *h):DataTransport(state,pt),hal(h),commands(pt,state),data(*this,state){pt.state=&state;assert(!commands.create(grant()));reserved=state.grant;}
 ~ItlIbssPeerPort(){assert(!calls&&!held&&!data.pending());++deleted;}
 bool inGate(){return hal->loop.gate;}
 bool onWorker(){return inGate()&&!hal->loop.work;}
 bool queueDrained(const PeerGrant &g){return !data.pending()&&!held&&pt.drained&&peer.ready&&samePeerGrant(g,reserved);}
 bool matching(unsigned slot){return match&&slot==3&&allocated&&!pt.drained;}
 void fault(){mismatch=true;peer.unknown=true;peer.ready=false;hal->awdlIbssExclusive.resetRequested=true;}
 bool prepareBuffer(const uint8_t *f,size_t n,Buffer &b){held=this;return DataTransport::prepareBuffer(f,n,b);}
 void discardBuffer(Buffer &b){held=nullptr;DataTransport::discardBuffer(b);}
 void reclaim(const PeerTxContext &c,Buffer &b){held=nullptr;DataTransport::reclaim(c,b);}
 bool stopProof(const PeerGrant &g){return stopEvidence&&pt.stopped&&g.generation==7;}
 bool stopProof(const PeerTxContext &c){return stopEvidence&&c.grant.generation==7&&matching(c.slot);}
};
static ItlIbssPeerPort *itlIbssPeerPort=nullptr;
static ItlIbss::DataTrial itlIbssDataTrial;
static ItlIwm *itlIbssDataOwner=nullptr;
static bool itlIbssNativeComplete(ItlIwm*,uint32_t,unsigned,unsigned,uint32_t){return true;}
static void itlIbssNativeTick(ItlIwm*){}
static void itlIbssNativeStop(ItlIwm*,bool){}
static bool itlIbssNativeRetainPeer(ItlIwm*,uint32_t,uint64_t){return false;}

#include "peer-runtime-methods.inc"
static std::vector<uint8_t> response(unsigned count=1,unsigned next=4,unsigned status=1){
 std::vector<uint8_t> bytes(sizeof(iwm_tx_resp)+count*sizeof(iwm_agg_tx_status)+4);
 auto r=reinterpret_cast<iwm_tx_resp*>(bytes.data());r->frame_count=count;
 if(count)r->status[0].status=uint16_t(status);memcpy(bytes.data()+sizeof(*r)+count*4,&next,4);return bytes;
}
static ItlIbssPeerPort *start(ItlIwm &h){auto p=new ItlIbssPeerPort(&h);itlIbssPeerPort=p;auto f=frame(p->reserved);assert(p->data.start(f.data(),f.size()));return p;}
static void stop(ItlIwm &h,ItlIbssPeerPort *p){p->pt.stopped=true;h.awdlIbssPeerStopped();assert(!itlIbssPeerPort);}
int main(){
 // Actual native batch-reuse wrapper requires the gate, real drain and unchanged addresses.
 for(unsigned fault=0;fault<6;++fault){ItlIwm h;auto p=start(h);auto r=response();
  assert(h.awdlIbssPeerTxComplete(10,3,r.data(),r.size()));auto next=frame(p->reserved);
  if(fault==1)p->calls=1;if(fault==2)p->retiring=true;if(fault==3)p->pt.drained=false;
  if(fault==4)next[4]^=2;if(fault==5)h.loop.work=true;
  const bool reused=itlIbssPeerNext(&h,next.data(),next.size());
  assert(reused==(fault==0));if(reused)assert(p->requestLength==next.size()&&!memcmp(p->request,next.data(),next.size())&&!p->peer.dataUsed);
  p->calls=0;p->retiring=false;p->pt.drained=true;h.loop.work=false;stop(h,p);
 }

 // Actual completion reclaims TX immediately; actual tick/bridge retains RX station without draining it.
 for(unsigned end=0;end<5;++end){ItlIwm h;auto p=start(h);auto r=response();
  itlIbssDataOwner=&h;auto &trial=itlIbssDataTrial;trial={};trial.phase=DataTrialPhase::Submitted;
  trial.generation=7;trial.sentNs=p->clock;trial.replyEndNs=p->clock+2500000000ULL;
  assert(h.awdlIbssPeerTxComplete(10,3,r.data(),r.size())&&p->reclaims==1&&!p->held&&!p->data.pending());
  const auto commands=p->pt.events.size();h.awdlIbssPeerTick();assert(p->peer.ready&&p->peer.stationMayExist&&!p->peer.released&&p->pt.events.size()==commands);
  p->clock=trial.replyEndNs-1;h.awdlIbssPeerTick();assert(p->peer.stationMayExist&&p->pt.events.size()==commands);
  if(end==0)p->clock=trial.replyEndNs;if(end==1)trial.cancelled=true;if(end==2)trial.endRequested=true;
  if(end==3)trial.generation++;if(end==4)trial.phase=DataTrialPhase::Recovering;
  h.awdlIbssPeerTick();assert(p->peer.released&&!p->peer.stationMayExist&&p->pt.releases==1&&p->reclaims==1);
  stop(h,p);itlIbssDataOwner=nullptr;trial={};
 }
 // A real hardware stop still frees a retained RX peer; no timer-based DMA reclamation.
 {ItlIwm h;auto p=start(h);auto r=response();itlIbssDataOwner=&h;auto &trial=itlIbssDataTrial;trial={};
  trial.phase=DataTrialPhase::Submitted;trial.generation=7;trial.sentNs=p->clock;trial.replyEndNs=p->clock+2500000000ULL;
  assert(h.awdlIbssPeerTxComplete(10,3,r.data(),r.size()));h.awdlIbssPeerTick();assert(!p->peer.released);
  stop(h,p);itlIbssDataOwner=nullptr;trial={};
 }
 for(unsigned status:{1u,2u,0x85u}){ItlIwm h;auto p=start(h);auto r=response(1,4,status);
  assert(!h.awdlIbssPeerTxComplete(9,3,r.data(),r.size())&&p->data.pending());
  h.loop.gate=false;assert(h.awdlIbssPeerTxComplete(10,3,r.data(),r.size())&&p->data.pending());h.loop.gate=true;
  assert(h.awdlIbssPeerTxComplete(10,3,r.data(),r.size())&&!p->data.pending()&&p->reclaims==1&&p->data.ackReported()==(status==1));
  h.awdlIbssPeerTick();assert(p->peer.released&&p->pt.releases==1);
  assert(h.awdlIbssPeerTxComplete(10,255,nullptr,0)&&p->reclaims==1&&!h.awdlIbssExclusive.resetRequested);
  stop(h,p);
 }
 // Every prefix is rejected before status/SSN access; owner retained until stop.
 auto good=response();for(size_t n=0;n<good.size();++n){ItlIwm h;auto p=start(h);std::vector<uint8_t> shortFrame(good.begin(),good.begin()+n);
  assert(h.awdlIbssPeerTxComplete(10,3,shortFrame.data(),n)&&h.awdlIbssExclusive.resetRequested&&p->data.pending()&&!p->reclaims);stop(h,p);
 }
 for(unsigned error=0;error<7;++error){ItlIwm h;auto p=start(h);auto r=response(error==0?0:error==1?2:1,error==2?5:4);
  if(error==3)h.com.sc_generation++;if(error==4)p->match=false;
  auto ptr=error==5?nullptr:r.data();unsigned slot=error==6?4:3;
  assert(h.awdlIbssPeerTxComplete(10,slot,ptr,r.size())&&h.awdlIbssExclusive.resetRequested&&p->data.pending()&&!p->reclaims);
  p->match=true;stop(h,p);
 }
 // Stop called inside a sleeping command must defer delete until outer return.
 {ItlIwm h;auto p=start(h);unsigned before=deleted;p->calls++;p->pt.stopped=true;h.awdlIbssPeerStopped();assert(itlIbssPeerPort==p&&deleted==before&&!p->data.pending());--p->calls;itlIbssPeerFinish(p);assert(!itlIbssPeerPort&&deleted==before+1);}
 // Stop inside prepare: private buffer is discarded by start, never by TX reset.
 {ItlIwm h;auto p=new ItlIbssPeerPort(&h);itlIbssPeerPort=p;++p->calls;auto f=frame(p->reserved);
  p->preparation=[&]{p->pt.stopped=true;h.awdlIbssPeerStopped();p->peer.ready=false;};
  assert(!p->data.start(f.data(),f.size())&&!p->held&&p->discards==1);--p->calls;itlIbssPeerFinish(p);assert(!itlIbssPeerPort);
 }
 puts("PASS actual native completion/stop wrappers + real peer/data cores: all truncated lengths, zero/multiple frame counts, wrong slot/SSN/generation, duplicate tombstone, SUCCESS vs DIRECT_DONE, pending stop and prepare reentry deferred deletion; ASan/UBSan. No RF.");
}
