#include "../../itlwm/awdl/IbssDataTrial.hpp"
#include <cassert>
#include <cstdio>
#include <functional>
static uint64_t clockNs=1000000000ULL;
static uint64_t itlAwdlRocNowNs(){return clockNs;}
static ItlIbss::DataTrial itlIbssDataTrial;
namespace ItlIbss {
struct PeerGrant {uint8_t peer[6]={};uint32_t generation=7,capabilityFlags=0;bool capabilitiesObserved=false;};
struct ExperimentObservation {
 enum Metric {TxAccepted,TxSubmitted,TxCompleted,TxReclaimed,TxStopReclaimed,TxDeadlineExpired,TxMismatch,TxCancelled,TxFlushRequested,TxFlushFailed,TxStatus,TxGeneration};
 uint32_t v[12]={};WindowProbe window;
 void set(Metric k,uint32_t x){v[k]=x;}uint32_t get(Metric k){return v[k];}
};
}
struct Counters {ItlIbss::ExperimentObservation ibss;void dataModelView(ItlAwdl::DataContext&) {}};
struct ItlIbssTxPort;
struct ItlIwm {
 struct Loop{bool gate=true,worker=true;bool inGate(){return gate;}bool onThread(){return !worker;}}loop;
 struct{uint32_t sc_generation=7;}com;
 struct{bool resetRequested=false,ownsRadio=true;uint64_t deadline=11000000000ULL;}awdlIbssExclusive;
 bool dmaDetaching=false,lock=true;uint32_t gp2=100000;unsigned starts=0,disposes=0;
 std::function<void()> duringStart;
 Counters counters,*awdlProbeCounters=&counters;ItlIbssTxPort *awdlIbssTxPort=nullptr;
 Loop *getMainWorkLoop(){return &loop;}bool awdlIbssDataActive(){return itlIbssDataTrial.active();}
 bool iwm_nic_lock(void*){return lock;}void iwm_nic_unlock(void*){}uint32_t iwm_read_prph(void*,unsigned){return gp2;}
 bool awdlIbssManagementDrained();void awdlIbssTxDispose();bool awdlIbssIdentityWindowValid();void awdlIbssScheduledTick();
 void awdlIbssExperimentEvent(unsigned,unsigned,int,unsigned,unsigned){}
};
static ItlIwm *itlIbssDataOwner=nullptr,*itlIbssFixedOwner=nullptr;
static ItlAwdl::FixedChannelControl itlIbssFixedControl;
static ItlAwdl::FixedControlView itlIbssFixedAdvertised;
static constexpr unsigned IWM_DEVICE_SYSTEM_TIME_REG=0;
struct ItlIbssTxPort {
 ItlIwm *hal;unsigned calls=0;bool pending=false;
 struct Adapter {
  ItlIbssTxPort &p;
  bool pending(){return p.pending;}
  bool start(const uint8_t *f,size_t n){
   assert(f&&n>=227&&p.hal->awdlIbssTxPort==&p&&itlIbssDataTrial.controlSending);
   if(p.hal->duringStart)p.hal->duringStart();
   if(!p.hal->awdlIbssIdentityWindowValid())return false;
   p.pending=true;++p.hal->starts;return true;
  }
 }adapter{*this};
 explicit ItlIbssTxPort(ItlIwm *h):hal(h){}
 void record(const char*,uint32_t){}
};
bool ItlIwm::awdlIbssManagementDrained(){using D=ItlIbss::ExperimentObservation;return awdlIbssTxPort&&!awdlIbssTxPort->pending&&counters.ibss.get(D::TxCompleted)==1&&counters.ibss.get(D::TxStatus)==1;}
void ItlIwm::awdlIbssTxDispose(){assert(awdlIbssManagementDrained());delete awdlIbssTxPort;awdlIbssTxPort=nullptr;++disposes;}
#include "scheduled-identity-method.inc"
#include "../../itlwm/awdl/IbssScheduledControlRuntime.hpp"
static void seed(ItlIwm &h){
 itlIbssDataTrial={};auto &t=itlIbssDataTrial;t.phase=ItlIbss::DataTrialPhase::Receiving;t.generation=7;t.controlMode=1;t.identityState=4;
 t.local[0]=2;t.local[5]=1;t.request.peer[0]=2;t.request.peer[5]=2;
 itlIbssDataOwner=itlIbssFixedOwner=&h;itlIbssFixedControl={};assert(itlIbssFixedControl.reset(t.local,7));
 ItlAwdl::SelfControlInput i;memcpy(i.source,t.request.peer,6);i.subtype=3;i.generation=i.currentGeneration=7;i.selfMetric=515;
 i.epochGp2=i.nowGp2=h.gp2;i.epochHostNs=i.nowHostNs=clockNs;i.leaseEndNs=h.awdlIbssExclusive.deadline;
 uint8_t frame[279];size_t n=0;assert(ItlAwdl::buildLinkIdentity(i,frame,sizeof(frame),n));
 assert(itlIbssFixedControl.observe(frame,n,7,h.gp2,clockNs));
}
int main(){using D=ItlIbss::ExperimentObservation;
 {ItlIwm h;seed(h);auto &t=itlIbssDataTrial;
  h.awdlIbssScheduledTick();assert(h.starts==1&&t.controlSchedule.pending&&t.controlSchedule.psf==1&&!t.controlSending);
  auto owned=h.awdlIbssTxPort;h.awdlIbssScheduledTick();assert(h.starts==1&&h.awdlIbssTxPort==owned);
  owned->pending=false;h.counters.ibss.set(D::TxCompleted,1);h.counters.ibss.set(D::TxStatus,1);
  clockNs+=1000000;h.gp2+=1000;h.awdlIbssScheduledTick();assert(t.controlSchedule.completed==1&&!t.controlSchedule.pending);
  clockNs=1000000000ULL+32*ItlAwdl::BoundedControlSchedule::Tu;h.gp2=100000+32*1024;t.phase=ItlIbss::DataTrialPhase::Submitted;
  h.awdlIbssScheduledTick();assert(h.starts==2&&h.disposes==1&&t.controlSchedule.mif==1);
  owned=h.awdlIbssTxPort;t.cancelled=true;h.awdlIbssScheduledTick();assert(t.controlSchedule.stopped&&h.awdlIbssTxPort==owned&&owned->pending);
  owned->pending=false;delete owned;
 }
 clockNs=1000000000ULL;
 for(unsigned bad=0;bad<7;++bad){ItlIwm h;seed(h);auto &t=itlIbssDataTrial;
  if(bad==0)t.controlMode=0;if(bad==1)t.passiveIbss=true;if(bad==2)h.loop.worker=false;
  if(bad==3)h.com.sc_generation=8;if(bad==4)t.phase=ItlIbss::DataTrialPhase::WaitingWindow;
  if(bad==5){clockNs+=600000000;h.gp2+=600000;}if(bad==6)h.awdlIbssExclusive.ownsRadio=false;
  h.awdlIbssScheduledTick();assert(!h.starts&&!h.awdlIbssTxPort);clockNs=1000000000ULL;
 }
 {ItlIwm h;seed(h);h.duringStart=[](){itlIbssDataTrial.cancelled=true;};h.awdlIbssScheduledTick();
  assert(!h.starts&&itlIbssDataTrial.endRequested&&itlIbssDataTrial.controlBlocked==9&&h.awdlIbssTxPort);
  delete h.awdlIbssTxPort; // mock has no published descriptor, production retains until stop proof
 }
 {ItlIwm h;seed(h);h.duringStart=[&](){
   auto &t=itlIbssDataTrial;ItlAwdl::SelfControlInput i;memcpy(i.source,t.request.peer,6);i.subtype=3;
   i.generation=i.currentGeneration=7;i.selfMetric=515;i.advertisementCounter=2;
   i.epochGp2=i.nowGp2=h.gp2;i.epochHostNs=i.nowHostNs=clockNs;i.leaseEndNs=h.awdlIbssExclusive.deadline;
   uint8_t frame[279];size_t n=0;assert(ItlAwdl::buildLinkIdentity(i,frame,sizeof(frame),n));
   assert(itlIbssFixedControl.observe(frame,n,7,h.gp2,clockNs));};
  h.awdlIbssScheduledTick();assert(!h.starts&&itlIbssDataTrial.controlBlocked==9&&itlIbssDataTrial.endRequested);
  delete h.awdlIbssTxPort;
 }
 {ItlIwm h;seed(h);auto &t=itlIbssDataTrial;t.controlMode=2;
  t.peerCapabilities.ht=0x016f;t.peerCapabilities.ampdu=31;t.peerCapabilities.mcs=0xffff;t.peerCapabilities.generation=7;t.peerCapabilities.observedNs=clockNs;
  ItlIbss::PeerGrant g;memcpy(g.peer,t.request.peer,6);itlIbssApplyObservedCapabilities(&h,g,2);
  assert(g.capabilitiesObserved&&g.capabilityFlags==(1u<<28|3u<<19|7u<<23)&&t.peerCapabilitiesApplied==1);
  g.capabilitiesObserved=false;t.controlMode=1;itlIbssApplyObservedCapabilities(&h,g,2);assert(!g.capabilitiesObserved);
  t.controlMode=2;g.generation=8;itlIbssApplyObservedCapabilities(&h,g,2);assert(!g.capabilitiesObserved);
 }
 puts("PASS actual scheduled runtime + admission revalidation: one owned ring, reclaim before reuse, PSF/MIF across data submission, cancellation retains pending DMA, worker/generation/lease/freshness/passive gates, independent observed HT mode");
}
