#include "trial-fixture.inc"
#include "FixedChannelControl.hpp"
#include <functional>
#include <map>
#include <string>
#include "BatchPolicy.hpp"
using namespace ItlIbss;
static uint64_t clockNs=1000000000;
static uint64_t itlAwdlRocNowNs(){return clockNs;}
enum class ExclusivePhase{Idle,FirmwareReady,Restoring,AwaitAssociation,Failed,Cancelled,Associated};
using IOReturn=int;constexpr int kIOReturnNotReady=-1,kIOReturnBadArgument=-2,kIOReturnNoMemory=-3,kIOReturnNoResources=-4,kIOReturnSuccess=0;
namespace ItlAwdl {struct {bool running=false;}gEngine;SingleDataProbe gSingleDataProbe;}
struct OSNumber{uint64_t value;static OSNumber *withNumber(uint64_t v,unsigned){return new OSNumber{v};}void release(){delete this;}};
struct OSDictionary{std::map<std::string,uint64_t> fields;static OSDictionary *withCapacity(unsigned){return new OSDictionary;}bool setObject(const char *k,OSNumber *n){fields[k]=n->value;return true;}void release(){delete this;}};
struct ItlAwdlProbeCounters:Counter {bool init(){return true;}void release(){}void configureManagementFilter(bool){}};
struct Controller{std::map<std::string,uint64_t> fields;std::map<std::string,OSDictionary> dictionaries;std::string fail;OSDictionary previous;
 OSDictionary *getProperty(const char*){previous.fields=fields;return &previous;}
 bool setProperty(const char *k,ItlAwdlProbeCounters*){return fail!=k;}
 bool setProperty(const char *k,OSDictionary *d){if(fail==k)return false;fields=d->fields;dictionaries[k]=*d;return true;}
};
struct OSObject{unsigned refs=1;void retain(){++refs;}void release(){assert(refs);--refs;}};
struct Gate:OSObject{void commandWakeup(bool*,bool){}};

struct IOTimerEventSource:OSObject{unsigned rearms=0;void setTimeoutMS(unsigned n){assert(n==250);++rearms;}};
struct ItlAwdlRocLifetime{OSObject *h,*g,*l;ItlAwdlRocLifetime(OSObject *a,OSObject *b,OSObject *c):h(a),g(b),l(c){h->retain();g->retain();l->retain();}~ItlAwdlRocLifetime(){l->release();g->release();h->release();}};
struct Loop:OSObject{bool gate=true,work=false;bool inGate(){return gate;}bool onThread(){return work;}};
namespace ItlAwdl {
struct NativeQueueOps {Loop *loop;};
struct TrialNativePort {
 OSObject *owner=nullptr;bool releasePending=false,nativeSession=false;
 unsigned rxCount=0;uint8_t registeredLocal[6]={},nativeSource[16]={};
 bool nativeStopPending=false,nativeAutoPeer=false;unsigned nativePeerGeneration=0,nativePeerEpoch=0;
 uint8_t nativePeerMAC[6]={},nativePeerIP[16]={};
 bool linkBusy=false,linkUp=false,linkDownPending=false;
 struct {bool pending=false;}hardware;
 struct {bool enabled=false,queued=false;bool empty()const{return !queued;}}tx;
 struct {bool busy=false,active=false;bool idle()const{return !busy&&!active;}
  bool stop(NativeQueueOps &){if(busy)return false;active=false;return true;}}queueActivation;
}gDataPort;
}
struct ItlIwm:OSObject {
 Loop loop;ItlAwdlProbeCounters counter;ItlAwdlProbeCounters *awdlProbeCounters=&counter;Controller controller;Gate gate;
 uint64_t awdlIbssWorkerEpoch=0;
 void *awdlIbssWorkerPrepare();bool awdlIbssWorkerSubmit(void *);void awdlIbssWorkerDiscard(void *);
 bool searchReserved=false;bool awdlNativeSearchReserved(){return searchReserved;}
 bool dmaDetaching=false,awdlIbssWorkPending=false,awdlProbeEnabled=true,awdlMgmtFilterEnabled=true,timerOK=true,canBegin=true,up=true,authorized=true;
 void *awdlIbssTxPort=nullptr;IOTimerEventSource *awdlIbssTimer=nullptr;BatchControl awdlBatch;
 ItlIwm(){memcpy(com.sc_ic.ic_myaddr,self,6);}
 Controller *getController(){return &controller;}Gate *getMainCommandGate(){return &gate;}
 bool peerActive=false;bool awdlIbssPeerActive(){return peerActive;}IOReturn awdlIbssExperimentTimerPrepare(){if(!timerOK)return kIOReturnNoResources;awdlIbssTimer=reinterpret_cast<IOTimerEventSource*>(this);return kIOReturnSuccess;}void awdlIbssExperimentTimerStart(){if(timerOK)awdlIbssTimer=reinterpret_cast<IOTimerEventSource*>(this);}
 IOReturn awdlIbssDataRequest(const SingleDataRequest &,bool,unsigned mode=0,bool nativeRequest=false);
 bool awdlIbssNativeStartIdle();
 IOReturn awdlIbssNativeRequest(const SingleDataRequest &,const uint8_t *);
 void awdlIbssNativeQuiesce();void awdlIbssNativeQueuesStop();
 bool nativeOpenOK=true;unsigned nativeOpens=0;
 bool awdlIbssNativeOpen(const uint8_t*){++nativeOpens;return nativeOpenOK;}
 void awdlListenerTxTick(){assert(false&&"legacy test cannot enter monitor injection");}
 void awdlIbssScheduledTick(){assert(false&&"legacy mode must not enter scheduled TX");}
 static void awdlIbssExperimentTimerAction(OSObject*,IOTimerEventSource*);
 static void awdlIbssExperimentWorker(void*,void*){}
 struct{ExclusivePhase phase=ExclusivePhase::Idle;bool ownsRadio=false,operation=false,resetRequested=false,resetProven=false;int primaryError=0,recoveryError=0;unsigned generation=0;struct{unsigned count=0;unsigned active()const{return count;}}ordinary;struct{bool busy=false;bool idle()const{return !busy;}}commandAuthority;uint64_t deadline=10000000000ULL,recoveryDeadline=0;struct{bool quotaConfigured=false,commandInFlight=false,needsReset=false;struct{bool allocated=false;bool valid()const{return allocated;}}resources;}session;}awdlIbssExclusive;
 struct{unsigned sc_generation=6,sc_flags=0;struct{uint8_t ic_myaddr[6]={};}sc_ic;struct{unsigned cur=0;}txq[32];}com;
 OneShot awdlIbssOneShot;unsigned preparations=0,submissions=0,ends=0,publishes=0;bool prepOK=true,submitOK=true;uint32_t gp2=102048;
 std::function<void()> prepareAction;
 Loop *getMainWorkLoop(){return &loop;}bool iwm_nic_lock(void*){return true;}void iwm_nic_unlock(void*){}uint32_t iwm_read_prph(void*,unsigned){return gp2;}
 void awdlIbssExperimentEvent(unsigned,unsigned,int,unsigned,unsigned){}
 bool awdlIbssDataActive();void awdlIbssDataPortResult(bool);void awdlIbssDataTick();void awdlIbssDataWindow();
 bool identityOK=true;int awdlIbssDataIdentity(bool);
 void awdlIbssDataPublish();
 void awdlIbssExclusiveEnd(){++ends;awdlIbssExclusive.ownsRadio=false;awdlIbssExclusive.phase=ExclusivePhase::AwaitAssociation;}
 bool awdlIbssPeerPrepare(const uint8_t *f,size_t n){assert(n==104&&ItlAwdl::dataPlainFrameValid(f,n));++preparations;if(prepareAction)prepareAction();return prepOK;}
 bool awdlIbssPeerSubmit(){++submissions;if(submitOK)com.txq[10].cur++;return submitOK;}
};
struct ItlIbssExclusiveTransport{ItlIwm *h;bool canBegin(){return h->canBegin;}bool administrativelyUp(){return h->up;}bool associatedAndAuthorized(){return h->authorized;}};
static unsigned nativeWindowPolls=0;
static void itlIbssNativeTick(ItlIwm *){++nativeWindowPolls;}
#define IWM_DEVICE_SYSTEM_TIME_REG 1
static DataTrial itlIbssDataTrial;static ItlIwm *itlIbssDataOwner=nullptr;
static ItlIwm *itlIbssFixedOwner=nullptr;static uint32_t itlIbssFixedGeneration=0;static ItlAwdl::FixedControlView itlIbssFixedAdvertised;
// This worker fixture supplies a fully completed identity coordinator.
int ItlIwm::awdlIbssDataIdentity(bool){if(!identityOK)return EAGAIN;itlIbssDataTrial.identityState=4;itlIbssDataTrial.identityEpochNs=clockNs;return 0;}
struct Port{ItlIwm *hal=nullptr;struct{unsigned queue=10;}reserved;
 struct{bool done=false,ack=false,pendingValue=false;uint32_t raw=UINT32_MAX;bool completed(){return done;}bool ackReported(){return ack;}uint32_t rawStatus(){return raw;}bool pending(){return pendingValue;}}data;
 bool mismatch=false;uint32_t noticeSlot=3,noticeLength=44,noticeCount=1,noticeSsn=4,noticeStatus=1;
}port;static Port *itlIbssPeerPort=&port;
struct ItlIbssWorkerRequest{ItlIwm *hal;Gate *gate;Loop *loop;void *call;uint64_t epoch;bool submitted;};
static std::vector<ItlIbssWorkerRequest*> jobs;
static void *IOMalloc(size_t n){return malloc(n);}static void IOFree(void *p,size_t){free(p);}
static void *thread_call_allocate(void(*)(void*,void*),void *arg){jobs.push_back(static_cast<ItlIbssWorkerRequest*>(arg));return arg;}
static void thread_call_enter(void*){}
static void thread_call_free(void*){}
static void finishJobs(){for(auto j:jobs){j->hal->awdlIbssWorkPending=false;j->loop->release();j->gate->release();j->hal->release();free(j);}jobs.clear();}
// Match actual kernel enum without pulling in native firmware command structs.
namespace ItlIbss {using ExclusivePhase=::ExclusivePhase;}
static bool batchReuseOK=true;
static unsigned batchReuses=0;
static bool itlIbssPeerNext(ItlIwm*,const uint8_t *p,size_t n){assert(p&&n==104);++batchReuses;return batchReuseOK;}
#include "trial-worker-methods.inc"
static SingleDataRequest request(Counter &c){
 auto f=fixture();assert(ingestAdvertisement(c.modelPeers,self,f.data(),f.size(),100000)==IngestResult::Accepted);
 le16(f,44,40);assert(ingestAdvertisement(c.modelPeers,self,f.data(),f.size(),101024)==IngestResult::Accepted);
 le16(f,44,39);assert(ingestAdvertisement(c.modelPeers,self,f.data(),f.size(),102048)==IngestResult::Accepted);
 auto &p=c.modelPeers.peers[0];p.diagnosticId=2;for(auto &e:p.channels.entries){e.channel=6;e.auxiliary=p.channels.encoding==1?0x2b:0x51;}
 SingleDataRequest r;r.token=99;memcpy(r.peer,p.address,6);r.sourceIp[0]=r.peerIp[0]=0xfe;r.sourceIp[1]=r.peerIp[1]=0x80;r.sourceIp[15]=1;r.peerIp[15]=2;return r;
}
static void setup(ItlIwm &h){port.hal=&h;port.data={};ItlAwdl::gEngine.running=false;ItlAwdl::gSingleDataProbe={};itlIbssDataTrial={};itlIbssDataOwner=&h;clockNs=1000000000;auto r=request(h.counter);assert(itlIbssDataTrial.arm(r,self,6,clockNs));}
int main(){
 // Failed timer reservation must not consume either a pristine or reusable native request.
 for(bool reused:{false,true}){
  ItlIwm a;setup(a);ItlAwdl::gDataPort={};ItlAwdl::gDataPort.owner=&a;auto r=itlIbssDataTrial.request;auto &q=itlIbssDataTrial;
  if(reused){q.nativeQueueMode=q.everArmed=q.cancelled=q.endRequested=q.nativeTeardownComplete=true;
   q.run=1;q.phase=DataTrialPhase::Cancelled;q.result=DataTrialResult::Cancelled;
   q.request.token=98;q.recovered=false;q.generation=0;q.nativeStarted=false;
   a.awdlIbssOneShot={};a.awdlIbssExclusive={};a.counter.ibss.recovery.phase=StaRecoveryPhase::Idle;
  }else{q={};itlIbssDataOwner=nullptr;}
  auto snapshot=q;auto exclusive=a.awdlIbssExclusive;auto *counters=a.awdlProbeCounters;auto *owner=itlIbssDataOwner;
  a.timerOK=false;
  assert(a.awdlIbssDataRequest(r,false,3,true)==kIOReturnNoResources);
  assert(!memcmp(&q,&snapshot,sizeof(q))&&a.awdlProbeCounters==counters&&itlIbssDataOwner==owner);
  assert(!memcmp(&a.awdlIbssExclusive,&exclusive,sizeof(exclusive))&&!a.awdlIbssOneShot.armed);
  assert(!a.awdlIbssTimer&&!a.awdlIbssWorkPending&&!a.preparations&&!a.submissions&&!a.ends);
 }

 // Native cancellation has a separate admission policy; diagnostic repeat stays terminal.
 for(bool untouched:{false,true})for(unsigned fault=0;fault<42;++fault){
  ItlIwm a;ItlAwdl::gDataPort={};ItlAwdl::gDataPort.owner=&a;auto req=request(a.counter);auto &q=itlIbssDataTrial;q={};itlIbssDataOwner=&a;
  ItlAwdl::gEngine.running=false;ItlAwdl::gSingleDataProbe={};
  q.everArmed=q.nativeQueueMode=q.cancelled=q.endRequested=q.nativeTeardownComplete=true;
  q.run=7;q.phase=DataTrialPhase::Cancelled;q.result=DataTrialResult::Cancelled;
  q.request=req;q.request.token=98;q.recovered=!untouched;q.generation=untouched?0:6;q.nativeStarted=!untouched;
  a.awdlIbssExclusive.phase=untouched?ExclusivePhase::Idle:ExclusivePhase::Associated;
  a.awdlIbssExclusive.resetProven=!untouched;a.awdlIbssOneShot.attempted=!untouched;
  a.counter.ibss.recovery.phase=untouched?StaRecoveryPhase::Idle:StaRecoveryPhase::StableAtDeadline;
  unsigned mode=3;bool native=true,cancel=false;
  if(fault==1)q.nativeQueueMode=false;if(fault==2)q.nativeTeardownComplete=false;
  if(fault==3)q.nativeError=5;if(fault==4)q.cancelled=false;if(fault==5)q.endRequested=false;
  if(fault==6)q.phase=DataTrialPhase::Failed;if(fault==7)q.result=DataTrialResult::RecoveryFailed;
  if(fault==8)q.run=0;if(fault==9)q.run=UINT32_MAX;if(fault==10)req.token=0;if(fault==11)req.token=q.request.token;
  if(fault==12){if(untouched)q.generation=6;else q.recovered=false;}
  if(fault==13)a.awdlIbssExclusive.phase=ExclusivePhase::FirmwareReady;
  if(fault==14)a.awdlIbssExclusive.ownsRadio=true;if(fault==15)a.awdlIbssExclusive.resetRequested=true;
  if(fault==16)a.awdlIbssExclusive.operation=true;if(fault==17)a.awdlIbssExclusive.ordinary.count=1;
  if(fault==18)a.awdlIbssExclusive.commandAuthority.busy=true;if(fault==19)a.awdlIbssExclusive.primaryError=1;
  if(fault==20)a.awdlIbssExclusive.recoveryError=1;if(fault==21)a.awdlIbssExclusive.session.resources.allocated=true;
  if(fault==22)a.awdlIbssExclusive.session.commandInFlight=true;if(fault==23)a.awdlIbssExclusive.session.needsReset=true;
  if(fault==24)a.counter.ibss.recovery.phase=StaRecoveryPhase::NotStable;
  if(fault==25)a.counter.ibss.control.lost=1;if(fault==26)a.counter.ibss.set(ExperimentObservation::RecoveryFaults,1);
  if(fault==27)a.awdlIbssWorkPending=true;if(fault==28)a.awdlIbssTxPort=&a;if(fault==29)a.peerActive=true;
  if(fault==30)ItlAwdl::gDataPort.hardware.pending=true;if(fault==31)a.canBegin=false;if(fault==32)a.up=false;if(fault==33)a.authorized=false;
  if(fault==34)itlIbssDataOwner=nullptr;if(fault==35)a.awdlProbeEnabled=false;
  if(fault==36)ItlAwdl::gEngine.running=true;if(fault==37)ItlAwdl::gSingleDataProbe.pending=true;
  if(fault==38)mode=1;if(fault==39)cancel=true;
  if(fault==40){if(untouched)q.nativeStarted=true;else a.awdlIbssOneShot.attempted=false;}
  if(fault==41)native=false;
  DataTrial before=q;auto counters=a.awdlProbeCounters;
  const auto status=a.awdlIbssDataRequest(req,cancel,mode,native);
  if(!fault){
   assert(status==kIOReturnSuccess&&q.run==8&&q.nativeQueueMode&&q.phase==DataTrialPhase::WaitingSta);
   assert(!q.cancelled&&!q.endRequested&&!q.nativeTeardownComplete&&q.request.token==99);
   assert(a.awdlProbeCounters!=counters&&!a.preparations&&!a.submissions&&!a.ends&&!a.awdlIbssWorkPending);
   assert(!a.awdlIbssOneShot.attempted&&a.awdlIbssOneShot.armed);
   auto fresh=q;
   assert(a.awdlIbssDataRequest(req,false,3,true)!=kIOReturnSuccess&&!memcmp(&q,&fresh,sizeof(q)));
  }else assert(status!=kIOReturnSuccess&&!memcmp(&q,&before,sizeof(q))&&a.awdlProbeCounters==counters);
 }

 // Actual native wrapper -> Quiesce -> queue teardown -> trial cancellation -> new native wrapper.
 for(bool enteredRadio:{false,true}){
  ItlIwm a;auto req=request(a.counter);itlIbssDataTrial={};itlIbssDataOwner=nullptr;
  ItlAwdl::gEngine.running=false;ItlAwdl::gSingleDataProbe={};auto &native=ItlAwdl::gDataPort;
  native={};native.owner=&a;memcpy(native.registeredLocal,self,6);
  assert(a.awdlIbssNativeRequest(req,self)==kIOReturnSuccess);
  auto &q=itlIbssDataTrial;assert(q.nativeQueueMode&&q.run==1&&q.phase==DataTrialPhase::WaitingSta);
  if(enteredRadio){
   q.phase=DataTrialPhase::Submitted;q.nativeStarted=q.prepared=true;q.generation=6;
   a.awdlIbssOneShot.attempted=true;a.awdlIbssExclusive.ownsRadio=true;
   a.awdlIbssExclusive.phase=ExclusivePhase::FirmwareReady;
  }
  a.awdlIbssNativeQuiesce();a.awdlIbssNativeQueuesStop();assert(q.nativeTeardownComplete);
  auto stopping=q;++req.token;
  assert(a.awdlIbssNativeRequest(req,self)!=kIOReturnSuccess&&!memcmp(&q,&stopping,sizeof(q)));
  a.awdlIbssDataTick();
  if(enteredRadio){
   assert(a.ends==1&&q.phase==DataTrialPhase::Recovering);
   a.awdlIbssExclusive.phase=ExclusivePhase::Associated;a.awdlIbssExclusive.resetProven=true;
   a.awdlProbeCounters->ibss.recovery.phase=StaRecoveryPhase::StableAtDeadline;
   a.awdlIbssDataPublish();assert(q.recovered);
  }
  assert(q.phase==DataTrialPhase::Cancelled&&q.result==DataTrialResult::Cancelled);
  native.nativeStopPending=true;native.nativeAutoPeer=true;native.nativePeerGeneration=99;
  assert(a.awdlIbssNativeRequest(req,self)==kIOReturnSuccess&&q.run==2&&q.request.token==req.token);
  assert(!native.nativeStopPending&&!native.nativeAutoPeer&&!native.nativePeerGeneration);
  assert(!q.cancelled&&!q.endRequested&&q.nativeQueueMode&&!q.nativeStarted&&!q.nativeTeardownComplete);
 }
 ItlAwdl::gDataPort={};
 puts("PASS actual native wrapper/Quiesce/QueuesStop/DataTick/Publish restart chain: pre-radio and restored-radio, no restart while stopping, fresh run/stop markers");
 puts("PASS native-only Stop/Start admission: untouched and recovered paths, 82 refusal cases preserve old trial/counters, diagnostic cancellation remains terminal");

 {DataTrial v;v.identityEpochGp2=1000;v.identityTxGp2=5096;v.identityEpochNs=1000000;
  assert(v.identityRefreshNotBeforeNs()==1000000+uint64_t(16*1024+4096)*1000);}
 // Execute the actual submit guard: stale/pending refresh must not touch data DMA.
 for(unsigned fault=0;fault<3;++fault){ItlIwm a;setup(a);auto &v=itlIbssDataTrial;
  v.phase=DataTrialPhase::WaitingWindow;v.prepared=true;v.identityState=fault==2?3:4;
  v.identityEpochNs=clockNs-(fault==1?50000001ULL:50000000ULL);
  a.counter.ibss.window.phase=WindowProbe::Open;a.awdlIbssDataWindow();
  assert(a.submissions==(fault==0?1u:0u));
  if(fault)assert(v.endRequested&&v.result==DataTrialResult::NoWindow);
 }

 // Execute actual passive worker path: full original lease, no prepare/submit, separate terminal success.
 for(unsigned mode=0;mode<2;++mode)for(unsigned scenario=0;scenario<4;++scenario){ItlIwm a;setup(a);auto &v=itlIbssDataTrial;v.listener=mode==0;v.passiveIbss=mode==1;v.listenerQueueReserved=mode==0;
  a.awdlIbssOneShot.attempted=true;a.awdlIbssExclusive.ownsRadio=true;a.awdlIbssExclusive.phase=ExclusivePhase::FirmwareReady;
  a.awdlIbssExclusive.session.quotaConfigured=true;a.awdlIbssExclusive.deadline=clockNs+20000000000ULL;
  a.awdlIbssDataTick();assert(v.listenerConfigured&&v.phase==DataTrialPhase::Receiving&&!a.preparations&&!a.submissions&&!a.ends);
  a.awdlIbssDataWindow();assert(!a.preparations&&!a.submissions);
  clockNs=a.awdlIbssExclusive.deadline-1;a.awdlIbssDataTick();assert(!a.ends);
  if(scenario==1)v.cancelled=true;if(scenario==2)a.awdlIbssExclusive.resetRequested=true;
  clockNs=a.awdlIbssExclusive.deadline;a.awdlIbssDataTick();assert(a.ends==1&&!a.preparations&&!a.submissions&&!v.ack&&!v.completed&&!v.sentNs);
  assert(v.listenerFinished==(scenario!=1&&scenario!=2));
  v.finishRecovery(scenario!=3);assert((v.phase==DataTrialPhase::Complete)==(scenario==0));
 }
 ItlIwm h;setup(h);auto &t=itlIbssDataTrial;
 assert(!t.arm(t.request,self,6,clockNs)&&t.phase==DataTrialPhase::WaitingSta);
 OneShot one;one.armed=true;assert(!one.due(1,true,6)&&!one.due(60000000000ULL,true,6)&&one.due(60000000001ULL,true,6)&&!one.due(70000000000ULL,true,6));
 t.generation=6;t.prepared=true;t.phase=DataTrialPhase::Submitting;WindowPlan plan;uint32_t reason;
 assert(h.counter.planIbssWindow(6,h.gp2,clockNs,10000000000ULL,self,plan,reason,t.request.peer));
 WindowProbe w;assert(w.arm(plan,6,h.gp2,clockNs,10000000000ULL));
 clockNs+=uint64_t(plan.open-h.gp2)*1000;assert(w.sample(6,plan.open,clockNs,10000000000ULL,true)==2);
 DataContext c;h.counter.dataModelView(c);c.gateHeld=true;c.channel=6;c.generation=6;c.gp2=plan.open;c.nowNs=clockNs;c.deadlineNs=10000000000ULL;
 assert(t.mayTransmit(c,w));
 // E284 live replay: a fresh advertisement moved the same boundary by 3us.
 {auto peer=h.counter.modelPeers;auto &p=peer.peers[0];p.diagnosticId=2;
  p.sync.awPeriod=16;p.sync.presenceMode=4;p.sync.nextAwSequence=605;p.sync.txDownCounter=46;
  p.syncReceivedGp2=p.lastSeenGp2=p.timing.receivedGp2=1608485;p.timing.txDelayUs=1700;
  p.timing.consistent=true;p.timing.streak=9;
  auto trial=t;auto context=c;auto window=w;context.peers=&peer;context.gp2=1656723;
  window.plan.peer=2;window.plan.boundary=1653886;window.plan.period=65536;
  window.plan.open=1655934;window.plan.close=1717374;window.plan.boundarySequence=608;window.plan.selectedSlot=8;
  WindowPrediction updated;assert(predictWindowWithTxDelay(p.sync,p.syncReceivedGp2,context.gp2,500000,1700,updated));
  assert(updated.nextBoundaryGp2-65536==1653889&&updated.nextBoundaryGp2-65536!=window.plan.boundary);
  assert(trial.mayTransmit(context,window));
  for(int shift:{-2052,-2051,2045,2046}){auto q=peer;q.peers[0].syncReceivedGp2+=shift;q.peers[0].lastSeenGp2+=shift;q.peers[0].timing.receivedGp2+=shift;
   auto a=context;a.peers=&q;a.gp2=window.plan.open+10000;
   assert(trial.mayTransmit(a,window)==(shift==-2051||shift==2045));
  }
  auto a=context;a.gp2=window.plan.open;assert(!trial.mayTransmit(a,window));
  auto changed=window;changed.plan.boundarySequence+=4;assert(!trial.mayTransmit(context,changed));
  changed=window;changed.plan.selectedSlot=7;assert(!trial.mayTransmit(context,changed));
  auto q=peer;q.peers[0].syncReceivedGp2-=6;q.peers[0].lastSeenGp2-=6;q.peers[0].timing.receivedGp2-=6;
  a=context;a.peers=&q;a.gp2=window.plan.close-5000;assert(!trial.mayTransmit(a,window));
  for(auto &x:peer.peers){x.syncReceivedGp2+=0xfff00000u;x.lastSeenGp2+=0xfff00000u;x.timing.receivedGp2+=0xfff00000u;}
  context.gp2+=0xfff00000u;window.plan.boundary+=0xfff00000u;window.plan.open+=0xfff00000u;window.plan.close+=0xfff00000u;
  assert(trial.mayTransmit(context,window));
 }
 for(unsigned bad=0;bad<11;++bad){auto a=c;auto window=w;auto trial=t;
  if(bad==0)trial.cancelled=true;if(bad==1)a.generation++;if(bad==2)a.modelEpoch++;if(bad==3)a.gp2=plan.close-4999;
  if(bad==4)window.plan.peer++;if(bad==5)a.nowNs=a.modelHostNs+500000001;if(bad==6)a.gateHeld=false;
  if(bad==7)trial.phase=DataTrialPhase::WaitingWindow;if(bad==8)trial.endRequested=true;if(bad==9)a.deadlineNs=a.nowNs+4999999;if(bad==10)trial.request.peer[5]^=2;
  assert(!trial.mayTransmit(a,window));
 }
 for(unsigned outcome=0;outcome<4;++outcome){auto trial=t;trial.completed=true;trial.ack=outcome!=0;trial.stopReclaimed=outcome==2;trial.cancelled=outcome==3;
  trial.result=DataTrialResult::AckReported;trial.finishRecovery(true);assert((trial.phase==DataTrialPhase::Complete)==(outcome==1));
  trial.finishRecovery(false);assert(!trial.recovered&&trial.result==DataTrialResult::RecoveryFailed);
 }
 // Actual worker: one prepare before a window, then one submit and bounded end.
 {ItlIwm a;setup(a);a.awdlIbssOneShot.attempted=true;a.awdlIbssExclusive.ownsRadio=true;a.awdlIbssExclusive.phase=ExclusivePhase::FirmwareReady;a.awdlIbssExclusive.session.quotaConfigured=true;
  a.awdlIbssDataTick();assert(a.preparations==1&&itlIbssDataTrial.phase==DataTrialPhase::WaitingWindow);
  a.awdlIbssDataTick();assert(a.preparations==1&&!a.submissions);
  a.counter.ibss.window.phase=WindowProbe::Open;a.awdlIbssDataWindow();a.awdlIbssDataWindow();assert(a.submissions==1&&itlIbssDataTrial.phase==DataTrialPhase::Submitted);
  clockNs=itlIbssDataTrial.sentNs+2000000000;a.awdlIbssDataTick();assert(a.ends==1&&itlIbssDataTrial.result==DataTrialResult::TimedOut&&itlIbssDataTrial.phase==DataTrialPhase::Recovering);
 }
 for(unsigned fault=0;fault<7;++fault){ItlIwm a;setup(a);
  if(fault<2){if(fault==0)itlIbssDataTrial.cancelled=true;else clockNs=itlIbssDataTrial.deadlineNs;a.awdlIbssDataTick();assert(!a.preparations&&!a.ends&&itlIbssDataTrial.phase==(fault==0?DataTrialPhase::Cancelled:DataTrialPhase::Failed));continue;}
  a.awdlIbssOneShot.attempted=true;a.awdlIbssExclusive.ownsRadio=true;a.awdlIbssExclusive.phase=ExclusivePhase::FirmwareReady;a.awdlIbssExclusive.session.quotaConfigured=true;
  if(fault==2)a.counter.modelGeneration++;if(fault==3)a.prepOK=false;if(fault==4)a.prepareAction=[&]{itlIbssDataTrial.cancelled=true;};if(fault==5)a.awdlIbssExclusive.ownsRadio=false;if(fault==6)a.awdlIbssExclusive.phase=ExclusivePhase::Failed;
  a.awdlIbssDataTick();assert(!a.submissions);
  if(fault==2)assert(!a.preparations&&itlIbssDataTrial.block==1);
  if(fault==3||fault==4){assert(itlIbssDataTrial.endRequested);a.awdlIbssDataTick();assert(a.ends==1);}
  if(fault==5)assert(itlIbssDataTrial.phase==DataTrialPhase::Recovering);if(fault==6)assert(itlIbssDataTrial.active()&&itlIbssDataTrial.result==DataTrialResult::RecoveryFailed&&!a.ends);
 }
 {ItlIwm a;setup(a);a.awdlIbssOneShot.stableSince=1;a.awdlIbssOneShot.stableGeneration=6;clockNs+=3000000000ULL;a.awdlIbssDataTick();assert(!a.awdlIbssOneShot.stableSince&&!a.awdlIbssOneShot.stableGeneration&&!a.preparations);}
 // Actual request: invalid preflight does not allocate a radio trial or budget.
 for(unsigned mode=1;mode<=4;++mode){ItlIwm a;setup(a);auto r=itlIbssDataTrial.request;itlIbssDataTrial={};itlIbssDataOwner=nullptr;
  assert(a.awdlIbssDataRequest(r,false,5)==kIOReturnBadArgument&&!itlIbssDataTrial.everArmed);
  assert(a.awdlIbssDataRequest(r,false,mode)==kIOReturnSuccess&&itlIbssDataTrial.controlMode==(mode==4?3:mode)&&itlIbssDataTrial.smallBatch.enabled==(mode==4));
  assert(!itlIbssDataTrial.controlSchedule.armed&&!a.preparations&&!a.submissions);
  assert(a.awdlIbssDataRequest(r,true,mode)==kIOReturnBadArgument&&!itlIbssDataTrial.cancelled);
  assert(a.awdlIbssDataRequest(r,true)==kIOReturnSuccess);a.awdlIbssDataTick();assert(!a.preparations&&!a.submissions);
 }
 for(unsigned bad=0;bad<8;++bad){ItlIwm a;setup(a);auto r=itlIbssDataTrial.request;itlIbssDataTrial={};itlIbssDataOwner=nullptr;
  if(bad==0)a.loop.gate=false;if(bad==1)a.canBegin=false;if(bad==2)a.awdlBatch.enabled=true;if(bad==3)ItlAwdl::gEngine.running=true;
  if(bad==4)a.awdlIbssOneShot.armed=true;if(bad==5)r.peer[0]=0xff;if(bad==6)a.controller.fail="AWDLRxProbe";if(bad==7)a.awdlProbeEnabled=false;
  assert(a.awdlIbssDataRequest(r,false)!=kIOReturnSuccess&&!itlIbssDataTrial.everArmed&&!a.preparations&&!a.submissions&&!a.ends);
 }
 {ItlIwm a;setup(a);auto r=itlIbssDataTrial.request;itlIbssDataTrial={};itlIbssDataOwner=nullptr;ItlAwdl::gEngine.running=false;
  assert(a.awdlIbssDataRequest(r,false)==kIOReturnSuccess&&itlIbssDataTrial.everArmed&&a.awdlIbssOneShot.armed&&a.awdlIbssTimer);
  assert(itlIbssDataTrial.request.sourceIp[8]==uint8_t(self[0]^2)&&itlIbssDataTrial.request.sourceIp[11]==0xff&&itlIbssDataTrial.request.sourceIp[12]==0xfe);
  assert(a.awdlIbssDataRequest(r,false)==kIOReturnNotReady&&!a.preparations&&!a.submissions);
  assert(a.awdlIbssDataRequest(r,true)==kIOReturnSuccess&&itlIbssDataTrial.cancelled);a.awdlIbssDataTick();assert(itlIbssDataTrial.phase==DataTrialPhase::Cancelled&&!a.ends);
 }
 for(unsigned bad=0;bad<4;++bad){ItlIwm a;setup(a);auto &trial=itlIbssDataTrial;trial.phase=DataTrialPhase::Recovering;trial.ack=trial.completed=true;trial.result=DataTrialResult::AckReported;
  a.awdlIbssExclusive.phase=ExclusivePhase::Associated;a.counter.ibss.recovery.phase=StaRecoveryPhase::StableAtDeadline;
  if(bad==1)a.awdlIbssExclusive.recoveryError=5;if(bad==2)trial.ack=false;if(bad==3)a.awdlIbssExclusive.phase=ExclusivePhase::Cancelled;a.awdlIbssDataPublish();
  assert((trial.phase==DataTrialPhase::Complete)==(bad==0)&&a.controller.fields["Phase"]==uint32_t(trial.phase));
 }
 // Publish ingress counters without depending on the finite raw/control row rings.
 {ItlIwm a;setup(a);auto &t=itlIbssDataTrial;using D=RxIngressDiagnostics;
  t.ingress.begin(D::Capture,true);t.ingress.record(D::DmaFailure,64,4096,true,UINT64_C(0x123456789));
  a.awdlIbssDataPublish();const auto &d=a.controller.dictionaries.at("AWDLIbssDataTrial").fields;
  assert(d.at("RxIngressVersion")==1&&d.at("RxIngressBuffersSeen")==1&&d.at("RxIngressBuffersCaptured")==1);
  assert(d.at("RxIngressDmaFailures")==1&&d.at("ReplyRxIngressDmaFailures")==1);
  assert(d.at("RxIngressFirstFault")==D::DmaFailure&&d.at("RxIngressFirstOffset")==64&&
   d.at("RxIngressFirstDetail")==4096&&d.at("RxIngressFirstHostNs")==UINT64_C(0x123456789));
 }
 // Failed admission records its first gate without creating or cancelling a trial.
 for(unsigned reason=8;reason<=21;++reason){ItlIwm a;setup(a);auto req=itlIbssDataTrial.request;
  itlIbssDataTrial={};itlIbssDataOwner=nullptr;a.com.sc_flags=0x239;
  if(reason==8)a.awdlBatch.enabled=true;if(reason==9)a.awdlIbssOneShot.armed=true;
  if(reason==10)a.awdlIbssWorkPending=true;if(reason==11)a.awdlIbssTxPort=&a;
  if(reason==12)a.peerActive=true;if(reason==13)a.awdlIbssExclusive.phase=ExclusivePhase::FirmwareReady;
  if(reason==14)a.awdlIbssExclusive.operation=true;if(reason==15)a.awdlIbssExclusive.ordinary.count=1;
  if(reason==16)a.awdlProbeEnabled=false;if(reason==17)ItlAwdl::gEngine.running=true;
  if(reason==18)ItlAwdl::gSingleDataProbe.pending=true;if(reason==19)a.canBegin=false;
  if(reason==20)a.up=false;if(reason==21)a.authorized=false;
  auto counters=a.awdlProbeCounters;
  assert(a.awdlIbssDataRequest(req,false,2)==kIOReturnNotReady);
  const auto &d=a.controller.dictionaries.at("AWDLIbssDataAdmission").fields;
  assert(d.at("Reason")==reason&&d.at("Token")==req.token&&d.at("Mode")==2&&
   d.at("Version")==1&&d.at("Generation")==6&&d.at("HostNs")==clockNs&&d.at("FirmwareFlags")==0x239);
  assert(!itlIbssDataTrial.everArmed&&a.awdlProbeCounters==counters&&!a.preparations&&!a.submissions&&!a.ends);
 }
 {ItlIwm a;setup(a);auto req=itlIbssDataTrial.request;itlIbssDataTrial={};itlIbssDataOwner=nullptr;
  a.awdlIbssWorkPending=true;a.controller.fail="AWDLIbssDataAdmission";
  assert(a.awdlIbssDataRequest(req,false)==kIOReturnNotReady&&!itlIbssDataTrial.everArmed);}
 // Repeated manual admission only after proven retirement/recovery; no firmware work in request.
 for(unsigned bad=0;bad<25;++bad){ItlIwm a;setup(a);auto &q=itlIbssDataTrial;
  q.run=1;q.phase=DataTrialPhase::Complete;q.recovered=true;q.result=DataTrialResult::AckReported;
  a.awdlIbssExclusive.phase=ExclusivePhase::Associated;a.awdlIbssExclusive.resetProven=true;
  a.awdlIbssOneShot.armed=a.awdlIbssOneShot.attempted=true;
  a.counter.ibss.recovery.phase=StaRecoveryPhase::StableAtDeadline;
  a.awdlIbssDataPublish();auto req=q.request;req.token++;
  if(bad==1)a.awdlIbssWorkPending=true;if(bad==2)a.awdlIbssTxPort=&a;if(bad==3)a.peerActive=true;
  if(bad==4)a.awdlIbssExclusive.resetProven=false;if(bad==5)a.awdlIbssExclusive.ownsRadio=true;
  if(bad==6)a.awdlIbssExclusive.commandAuthority.busy=true;if(bad==7)a.awdlIbssExclusive.session.resources.allocated=true;
  if(bad==8)a.awdlIbssExclusive.session.commandInFlight=true;if(bad==9)a.awdlIbssExclusive.resetRequested=true;
  if(bad==10)a.counter.ibss.recovery.phase=StaRecoveryPhase::Watching;if(bad==11)req.token=q.request.token;
  if(bad==12)q.cancelled=true;if(bad==13)q.phase=DataTrialPhase::Recovering;if(bad==14)a.counter.ibss.control.lost=1;
  if(bad==15)a.counter.ibss.set(ExperimentObservation::RecoveryFaults,1);if(bad==16)a.controller.fail="AWDLPreviousIbssDataTrial";
  if(bad==17)a.awdlIbssExclusive.ordinary.count=1;
  if(bad==18)a.awdlIbssExclusive.session.needsReset=true;if(bad==19)a.awdlIbssExclusive.primaryError=5;
  if(bad==20)a.awdlIbssExclusive.recoveryError=5;if(bad==21)q.recovered=false;if(bad==22)q.run=UINT32_MAX;
  if(bad==23)itlIbssDataOwner=nullptr;
  if(bad==24)a.awdlIbssOneShot.attempted=false;
  auto oldCounters=a.awdlProbeCounters;auto result=a.awdlIbssDataRequest(req,false);
  if(!bad){assert(result==kIOReturnSuccess&&q.run==2&&q.request.token==req.token&&q.phase==DataTrialPhase::WaitingSta);
   assert(a.awdlProbeCounters!=oldCounters&&a.awdlIbssExclusive.phase==ExclusivePhase::Idle&&!a.awdlIbssExclusive.resetProven);
   assert(!a.awdlIbssOneShot.attempted&&!a.preparations&&!a.submissions&&!a.ends);
   assert(a.awdlIbssDataRequest(req,false)==kIOReturnNotReady); // active-run replay
   for(unsigned run=3;run<=5;++run){q.phase=DataTrialPhase::Failed;q.recovered=true;q.result=DataTrialResult::NoWindow;
    a.awdlIbssExclusive.phase=ExclusivePhase::Associated;a.awdlIbssExclusive.resetProven=true;
    a.awdlIbssOneShot.attempted=true;a.awdlProbeCounters->ibss.recovery.phase=StaRecoveryPhase::StableAtDeadline;
    a.awdlIbssDataPublish();++req.token;
    assert(a.awdlIbssDataRequest(req,false)==kIOReturnSuccess&&q.run==run&&!a.awdlIbssOneShot.attempted);
   }
  }else{assert(result!=kIOReturnSuccess&&q.run==(bad==22?UINT32_MAX:1)&&a.awdlProbeCounters==oldCounters);
   if(bad!=16){const unsigned reasons[]={0,10,11,12,3,3,3,3,3,3,4,2,2,2,5,6,0,3,3,3,3,2,2,1,7};
    const auto &d=a.controller.dictionaries.at("AWDLIbssDataAdmission").fields;
    assert(d.at("Reason")==reasons[bad]&&d.at("Token")==req.token);
   }
  }
 }
 // Actual completion metadata keeps timeout/cancellation sticky while allowing reclamation.
 for(unsigned late=0;late<4;++late){ItlIwm a;setup(a);auto &trial=itlIbssDataTrial;trial.generation=6;trial.phase=DataTrialPhase::Submitted;trial.sentNs=clockNs;
  clockNs+=late==0?1000000000ULL:2000000000ULL;port.data.done=port.data.ack=true;port.data.raw=1;
  if(late==2){trial.cancelled=true;trial.result=DataTrialResult::Cancelled;}
  if(late==3)trial.result=DataTrialResult::TimedOut;
  a.awdlIbssDataPortResult(false);assert(trial.completed&&trial.ack&&trial.endRequested);
  trial.finishRecovery(true);assert((trial.phase==DataTrialPhase::Complete)==(late==0));
  if(late==1||late==3)assert(trial.result==DataTrialResult::TimedOut);
  if(late==2)assert(trial.result==DataTrialResult::Cancelled);
 }
 // Actual completion keeps an ACK-only listen window, never renewing it on duplicate callbacks.
 {ItlIwm a;setup(a);auto &trial=itlIbssDataTrial;trial.generation=6;trial.phase=DataTrialPhase::Submitted;trial.sentNs=clockNs;
  a.awdlIbssExclusive.ownsRadio=true;a.awdlIbssExclusive.phase=ExclusivePhase::FirmwareReady;
  assert(trial.beginReply(a.gp2,a.awdlIbssExclusive.deadline));
  clockNs+=100000000;port.data.done=port.data.ack=true;port.data.raw=1;a.awdlIbssDataPortResult(false);
  assert(trial.completed&&trial.ack&&!trial.endRequested&&trial.replyEndNs==trial.sentNs+2500000000ULL);
  const auto deadline=trial.replyEndNs;clockNs+=100000000;a.awdlIbssDataPortResult(false);assert(trial.replyEndNs==deadline);
  clockNs=deadline-1;a.awdlIbssDataTick();assert(!a.ends&&trial.result==DataTrialResult::AckReported);
  clockNs=deadline;a.awdlIbssDataTick();assert(a.ends==1&&trial.result==DataTrialResult::AckReported&&!a.submissions);
 }
 // Reply observer is independent of AUX globals; wrong/stale/duplicate/cancelled input cannot satisfy it.
 {ItlIwm a;setup(a);auto &trial=itlIbssDataTrial;trial.generation=6;trial.phase=DataTrialPhase::Submitted;trial.sentNs=clockNs;
  assert(trial.beginReply(a.gp2,clockNs+1000000000ULL));
  uint8_t eth[78]={},wire[104];size_t size=0;
  memcpy(eth,trial.local,6);memcpy(eth+6,trial.request.peer,6);eth[12]=0x86;eth[13]=0xdd;
  memcpy(eth+14,trial.replyObserver.frame+40,64);auto ip=eth+14;
  memcpy(ip+8,trial.request.peerIp,16);memcpy(ip+24,trial.request.sourceIp,16);
  ip[40]=129;ip[42]=ip[43]=0;auto sum=probeChecksum(ip);ip[42]=sum>>8;ip[43]=sum;
  assert(encodeData(eth,sizeof(eth),trial.request.peer,1,1,wire,sizeof(wire),size)==DataError::Ok);
  for(unsigned fault=0;fault<7;++fault){auto q=trial;auto bytes=std::vector<uint8_t>(wire,wire+104);uint32_t gen=6,ch=6,gp2=a.gp2+100;auto time=clockNs+100000;
   if(fault==0)gen++;if(fault==1)ch=44;if(fault==2)bytes[100]^=1;if(fault==3)time=q.replyEndNs;
   if(fault==4)q.cancelled=true;if(fault==5)q.phase=DataTrialPhase::Recovering;if(fault==6)q.endRequested=true;
   assert(!q.observeReply(bytes.data(),bytes.size(),gen,ch,gp2,time));
  }
  assert(trial.observeReply(wire,104,6,6,a.gp2+750000,clockNs+750000000));
  assert(!trial.observeReply(wire,104,6,6,a.gp2+100,clockNs+100000));
  assert(!ItlAwdl::gSingleDataProbe.replyObserved);
 }
 // Identity completion is a prerequisite; preserve advertised residency without delaying cancellation/faults.
 {ItlIwm a;setup(a);a.awdlIbssOneShot.attempted=true;a.awdlIbssExclusive.ownsRadio=true;a.awdlIbssExclusive.phase=ExclusivePhase::FirmwareReady;a.awdlIbssExclusive.session.quotaConfigured=true;a.identityOK=false;
  a.awdlIbssDataTick();assert(!a.preparations&&!a.submissions&&!itlIbssDataTrial.endRequested);
 }
 for(unsigned fault=0;fault<3;++fault){ItlIwm a;setup(a);auto &trial=itlIbssDataTrial;
  a.awdlIbssExclusive.ownsRadio=true;a.awdlIbssExclusive.phase=ExclusivePhase::FirmwareReady;
  trial.phase=DataTrialPhase::Submitted;trial.sentNs=clockNs;trial.completed=trial.ack=true;trial.endRequested=true;trial.result=DataTrialResult::AckReported;trial.identityMinEndNs=clockNs+1000000000ULL;
  if(fault==1)trial.cancelled=true;if(fault==2)a.awdlIbssExclusive.resetRequested=true;
  a.awdlIbssDataTick();assert(a.ends==(fault?1u:0u));
  if(!fault){clockNs=trial.identityMinEndNs;a.awdlIbssDataTick();assert(a.ends==1);}
 }
 // Actual timer stops dispatching after cancel-before-launch, keeps recovery alive.
 {ItlIwm a;setup(a);IOTimerEventSource timer;a.awdlIbssTimer=&timer;
  ItlIwm::awdlIbssExperimentTimerAction(&a,&timer);assert(jobs.empty()&&!timer.rearms);
  a.awdlIbssOneShot.armed=true;ItlIwm::awdlIbssExperimentTimerAction(&a,&timer);assert(jobs.size()==1&&timer.rearms==1);
  ItlIwm::awdlIbssExperimentTimerAction(&a,&timer);assert(jobs.size()==1&&timer.rearms==2);finishJobs();
  a.awdlIbssOneShot.armed=false;ItlIwm::awdlIbssExperimentTimerAction(&a,&timer);assert(jobs.empty()&&timer.rearms==2);
  a.counter.ibss.recovery.phase=StaRecoveryPhase::Watching;ItlIwm::awdlIbssExperimentTimerAction(&a,&timer);assert(jobs.size()==1&&timer.rearms==3);finishJobs();
  assert(a.refs==1&&a.loop.refs==1&&a.gate.refs==1&&timer.refs==1);
 }
 puts("PASS real wire peer -> targeted window -> trial eligibility, invalid/stale/wrong-peer/window guards, once budget, existing 60s baseline, actual worker prepare/one submit/2s timeout/cancel/bootstrap failure, sticky late ACK/timeout/cancel, ACK and restored-only terminal success; ASan/UBSan, no RF");
}
