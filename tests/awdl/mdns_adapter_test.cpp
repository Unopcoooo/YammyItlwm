
#include <cassert>
#include <cstdio>
#include <vector>
#include <functional>
#include <cstring>
#include "contracts.hpp"
#include "SingleFrameAdapter.hpp"
using namespace ItlIbss;
struct Transport {
 struct Buffer{uint64_t commandAddress=0xc12345000ull,scratchAddress=0xc12345000ull+sizeof(iwm_cmd_header)+offsetof(iwm_tx_cmd,scratch);Segment segments[2]={{0xa12345000ull,8},{0xb12345000ull,8}};unsigned count=2;uint8_t *frame=nullptr;size_t length=0;};
 bool gate=true,healthy=true,ringEmpty=true,proof=false,flushOK=true,partialFail=false,invalidDMA=false,reverseValidation=false;
 uint32_t gen=20;uint64_t clock=100;unsigned slot=63,revalidations=0,failValidation=0,allocated=0,freed=0,staged=0,bells=0,flushes=0;
 bool owned=false;std::vector<int> events;std::function<void()> duringPrepare,onBell,onFlush;
 void record(const char *,uint32_t){}
 bool inGate(){return gate;}uint32_t generation(){return gen;}uint64_t now(){return clock;}
 bool snapshot(TxContext &c){c.resources={1,1,1,3,4,3,9,true};c.generation=gen;c.now=clock;c.leaseEnd=10000000000ull;c.slot=slot;c.healthy=healthy;c.ringEmpty=ringEmpty;c.antenna=2;c.coexistPriority=1;return true;}
 bool prepareBuffer(const uint8_t *f,size_t n,Buffer &b){events.push_back(1);assert(!owned);b.frame=new uint8_t[n];b.length=n;b.segments[1].length=uint32_t(n-32);memcpy(b.frame,f,n);owned=true;++allocated;
  if(invalidDMA)b.segments[0].length=4096;
  if(duringPrepare)duringPrepare();return !partialFail;
 }
 bool revalidate(const TxContext &c,uint64_t &n){events.push_back(2);n=clock;++revalidations;if(reverseValidation)n=revalidations==1?200:150;return revalidations!=failValidation&&healthy&&ringEmpty&&c.generation==gen&&c.slot==slot&&n<c.leaseEnd;}
 void stage(const TxContext &c,Buffer &b,const iwm_device_cmd &cmd,const iwm_tfd &tfd,uint16_t scheduler){
  assert(gate&&owned&&c.generation==gen&&c.slot==slot);assert(cmd.hdr.qid==3&&cmd.hdr.idx==slot&&tfd.num_tbs==4&&scheduler==ItlAwdl::auxUnencryptedSchedulerEntry(uint16_t(b.length),3));
  assert(!memcmp(cmd.data+sizeof(iwm_tx_cmd),b.frame,24));assert(reinterpret_cast<const iwm_tx_cmd*>(cmd.data)->sta_id==3);
  ++staged;events.push_back(3);ringEmpty=false; // mock ring metadata, BC + duplicate, cur/queued, DMA barrier
 }
 void doorbell(const TxContext &){assert(staged&&owned);++bells;events.push_back(4);if(onBell)onBell();}
 void release(Buffer &b){assert(owned&&b.frame);owned=false;++freed;delete[] b.frame;b.frame=nullptr;}
 void discardBuffer(Buffer &b){assert(!staged);events.push_back(5);release(b);}
 void reclaim(const TxContext &,Buffer &b){assert(staged);events.push_back(6);release(b);ringEmpty=true;}
 bool flush(uint32_t mask){assert(mask==8);++flushes;events.push_back(7);if(onFlush)onFlush();return flushOK;}
 bool stopProof(const TxContext &c){return proof&&c.generation==20;}
};
using Adapter=SingleFrameAdapter<Transport,false,true>;using C=Adapter::Completion;
const uint8_t packet[]={0x33,0x33,0x00,0x00,0x00,0xfb,0x02,0x01,0x02,0x03,0x04,0x05,0x86,0xdd,0x60,0x00,0x00,0x00,0x00,0x2d,0x11,0xff,0xfe,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x12,0x34,0xff,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xfb,0x14,0xe9,0x14,0xe9,0x00,0x2d,0x4e,0x19,0x12,0x34,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x5f,0x61,0x69,0x72,0x64,0x72,0x6f,0x70,0x04,0x5f,0x74,0x63,0x70,0x05,0x6c,0x6f,0x63,0x61,0x6c,0x00,0x00,0x0c,0x00,0x01};
uint8_t frame[sizeof(packet)+26];
int main(){
 const uint8_t source[6]={2,1,2,3,4,5};size_t used=0;assert(ItlAwdl::encodeData(packet,sizeof(packet),source,0,0,frame,sizeof(frame),used)==ItlAwdl::DataError::Ok&&used==sizeof(frame));
 {Transport t;Adapter a(t);t.onBell=[&]{assert(a.pending());assert(a.complete(20,3,63)==C::Reclaim);};
  assert(a.start(frame,sizeof(frame))&&t.bells==1&&t.freed==1&&!a.pending());assert(t.events==std::vector<int>({1,2,2,3,4,6}));
  assert(!a.start(frame,sizeof(frame))&&a.complete(20,3,63)==C::Consume&&t.freed==1);
 }
 // Gate or precondition failure must not allocate or publish.
 for(unsigned bad=0;bad<3;++bad){Transport t;Adapter a(t);if(bad==0)t.gate=false;if(bad==1)t.healthy=false;if(bad==2)t.ringEmpty=false;assert(!a.start(frame,sizeof(frame))&&!t.allocated&&!t.bells);}
 // Fail partial preparation; cancel/generation/slot/health changes during a
 // gate-releasing allocation; either pre-publication revalidation; bad DMA.
 for(unsigned bad=0;bad<11;++bad){Transport t;Adapter a(t);
  if(bad==0)t.partialFail=true;
  if(bad==1)t.duringPrepare=[&]{a.cancel();};
  if(bad==2)t.duringPrepare=[&]{++t.gen;};
  if(bad==3)t.duringPrepare=[&]{++t.slot;};
  if(bad==4)t.duringPrepare=[&]{t.healthy=false;};
  if(bad==5)t.duringPrepare=[&]{t.clock=10000000000ull;};
  if(bad==6)t.failValidation=1;if(bad==7)t.failValidation=2;if(bad==8)t.invalidDMA=true;
  if(bad==9)t.duringPrepare=[&]{assert(!a.start(frame,sizeof(frame)));assert(a.complete(20,3,63)==C::Consume);};
  if(bad==10)t.reverseValidation=true;
  bool result=a.start(frame,sizeof(frame));
  if(bad==9){assert(result);assert(a.complete(20,3,63)==C::Reclaim);}else assert(!result&&!t.staged&&!t.bells);
  assert(t.allocated==1&&t.freed==1&&!a.pending());
 }
 {Transport t;Adapter a(t);assert(a.start(frame,sizeof(frame)));t.gate=false;assert(a.complete(20,3,63)==C::Defer&&a.pending()&&!t.freed);t.gate=true;assert(a.complete(20,3,63)==C::Reclaim);}
 // Unknown doorbell outcome keeps ownership; flush ACK does not free.
 {Transport t;Adapter a(t);assert(a.start(frame,sizeof(frame))&&a.pending());a.cancel();assert(t.flushes==1&&t.freed==0&&a.pending());
  a.tick();a.cancel();assert(t.flushes==1);assert(!a.hardwareStopped()&&a.pending());
  t.clock=3000000000ull;a.tick();assert(a.pending()&&t.freed==0);
  assert(a.complete(21,3,63)==C::Consume&&a.pending());assert(a.complete(20,9,63)==C::Unowned);
  assert(a.complete(20,3,63)==C::Reclaim&&!a.pending()&&t.freed==1);assert(a.complete(20,3,63)==C::Consume);
 }
 // Completion/real stop can race synchronous flush; late return cannot free twice.
 for(unsigned finish=0;finish<3;++finish){Transport t;Adapter a(t);assert(a.start(frame,sizeof(frame)));t.flushOK=false;
  t.onFlush=[&]{if(finish==0)assert(a.complete(20,3,63)==C::Reclaim);if(finish==1){t.proof=true;assert(a.hardwareStopped());}};
  a.cancel();if(finish==2){assert(a.pending()&&t.freed==0);t.proof=true;assert(a.hardwareStopped());}
  assert(!a.pending()&&t.freed==1&&t.flushes==1);assert(a.complete(20,3,63)==C::Consume);assert(a.hardwareStopped()==t.proof);assert(t.freed==1);
 }
 {Transport t;Adapter a(t);assert(a.start(frame,sizeof(frame)));++t.gen;a.tick();assert(a.pending()&&!t.flushes&&!t.freed);assert(!a.hardwareStopped());t.proof=true;assert(a.hardwareStopped()&&t.freed==1);}
 // Caller storage can change while prepare releases gate: command uses the
 // already frozen copy, not the caller's mutated frame.
 {Transport t;Adapter a(t);uint8_t copy[sizeof(frame)];memcpy(copy,frame,sizeof(frame));t.duringPrepare=[&]{copy[0]=0;};assert(a.start(copy,sizeof(frame)));assert(a.complete(20,3,63)==C::Reclaim);}
 puts("PASS connected adapter: stage/barrier/doorbell order; immediate completion; partial allocation rollback; cancel/reentrant call and generation/slot/health/lease changes during preparation; two publication guards; bad DMA; frozen input; unknown publication/flush ownership; late/duplicate completion; completion or real-stop during flush; stop-proof refusal; exactly-once actual buffer delete");
}
