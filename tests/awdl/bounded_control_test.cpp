#include "../../itlwm/awdl/BoundedControlSchedule.hpp"
#include "../../itlwm/awdl/ObservedPeerCapabilities.hpp"
#include "../../itlwm/awdl/IbssDataTrial.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
#include <fstream>
#include <iterator>
using namespace ItlAwdl;
static std::vector<uint8_t> advert(unsigned mcsBytes=1){
 std::vector<uint8_t> p(40+3+7+mcsBytes);p[0]=0xd0;p[10]=2;p[24]=127;p[26]=0x17;p[27]=0xf2;p[28]=8;p[29]=0x10;p[30]=3;
 p[40]=7;p[41]=uint8_t(7+mcsBytes);p[45]=0xce;p[46]=0x11;p[47]=0x1b;
 for(unsigned i=0;i<mcsBytes;++i)p[48+i]=255;
 return p;
}
int main(int argc,char **argv){
 using C=BoundedControlSchedule;const uint64_t epoch=1000000000ULL;
 C c;assert(!c.arm(0,epoch,epoch+1000000000,64));assert(c.arm(7,epoch,epoch+10000000000ULL,64));
 assert(!c.arm(7,epoch,epoch+10000000000ULL,64));assert(c.due(7,epoch,true)==C::Psf);
 assert(c.publish(C::Psf,7,epoch,64));assert(c.due(7,epoch+1000000,true)==C::None);
 assert(!c.publish(C::Mif,7,epoch+1000000,32));assert(c.complete(true));assert(!c.complete(true));
 assert(c.due(7,epoch+32*C::Tu,false)==C::None);
 assert(c.due(7,epoch+32*C::Tu,true)==C::Mif);assert(c.publish(C::Mif,7,epoch+32*C::Tu,32));assert(c.complete(true));
 // A late worker skips old intervals, then waits for completion and the next deadline.
 const auto late=epoch+900000000ULL;assert(c.due(7,late,true)==C::Mif);
 assert(c.publish(C::Mif,7,late,64));assert(c.nextMif==late+32*C::Tu);assert(c.complete(true));
 assert(c.publish(C::Psf,7,late,64));assert(c.nextPsf==epoch+8*C::PsfPeriod);assert(c.complete(true));
 // Compare late wakes with Owly's deadline-stepping rule, including exact boundaries.
 for(uint64_t delay: {0ULL,1ULL,1024000ULL,C::PsfPeriod-1,C::PsfPeriod,9*C::PsfPeriod+123}){
  C phase;assert(phase.arm(7,epoch,epoch+10000000000ULL,64));phase.nextMif=phase.endNs;
  uint64_t expected=epoch;while(expected<=epoch+delay)expected+=C::PsfPeriod;
  assert(phase.publish(C::Psf,7,epoch+delay,64));assert(phase.nextPsf==expected);
  assert(phase.complete(true));assert(phase.due(7,epoch+delay,true)==C::None);
 }
 // A deadline beyond the lease must not wrap into an immediate extra send.
 {C top;const auto start=UINT64_MAX-20000000000ULL;
  assert(top.arm(7,start,UINT64_MAX,64));top.nextMif=top.endNs;
  assert(top.publish(C::Psf,7,UINT64_MAX-1,64));assert(top.nextPsf==top.endNs);
  assert(top.complete(true));assert(top.due(7,UINT64_MAX-1,true)==C::None);
 }
 assert(c.due(8,late,true)==C::None&&c.stopped);
 C cancel;assert(cancel.arm(7,epoch,epoch+10000000000ULL,64));cancel.stop();assert(cancel.due(7,epoch,true)==C::None);
 C clock;assert(clock.arm(7,epoch,epoch+10000000000ULL,64));assert(clock.due(7,epoch-1,true)==C::None&&clock.stopped);
 C end;assert(end.arm(7,epoch,epoch+10000000000ULL,64));assert(end.due(7,end.endNs,true)==C::None&&end.stopped);
 C limit;assert(limit.arm(7,epoch,epoch+10000000000ULL,64));limit.submitted=256;assert(limit.due(7,epoch,true)==C::Psf&&!limit.stopped);
 C failed;assert(failed.arm(7,epoch,epoch+10000000000ULL,64));assert(failed.publish(C::Psf,7,epoch,64));assert(!failed.complete(false)&&failed.stopped&&!failed.pending);
 C overflow;assert(!overflow.arm(7,UINT64_MAX-10,UINT64_MAX,64));
 const uint8_t peer[6]={2,0,0,0,0,0};ObservedPeerCapabilities h;
 for(unsigned bytes=1;bytes<=4;++bytes){auto p=advert(bytes);assert(h.observe(p.data(),p.size(),peer,7,epoch));assert(h.fresh(7,epoch+500000000));assert(!h.fresh(7,epoch+500000001));}
 assert(!h.fresh(8,epoch)&&!h.fresh(7,epoch-1));
 auto p=advert(2);assert(h.observe(p.data(),p.size(),peer,7,epoch));
 assert(h.stationFlags(1)==((3u<<19)|(6u<<23)));
 assert(h.stationFlags(2)==((1u<<28)|(3u<<19)|(6u<<23)));
 p[45]&=uint8_t(~12);assert(h.observe(p.data(),p.size(),peer,7,epoch));assert(!(h.stationFlags(2)&(1u<<28)));
 p[45]|=4;assert(h.observe(p.data(),p.size(),peer,7,epoch));assert(h.stationFlags(2)&(1u<<17));
 const auto saved=h.mcs;
 auto good=advert();
 for(size_t n=0;n<good.size();++n)assert(!h.observe(good.data(),n,peer,7,epoch));
 p=good;p.insert(p.end(),good.begin()+40,good.end());assert(!h.observe(p.data(),p.size(),peer,7,epoch));
 p=good;p[10]=4;assert(!h.observe(p.data(),p.size(),peer,7,epoch));
 p=good;p[47]|=0x80;assert(!h.observe(p.data(),p.size(),peer,7,epoch));
 p=good;p[45]=uint8_t((p[45]&~12)|8);assert(!h.observe(p.data(),p.size(),peer,7,epoch));
 assert(h.mcs==saved);
 {ItlIbss::DataTrial t;t.local[0]=2;t.request.peer[0]=4;RawRxMetadata m;
  m.descriptorValid=m.lengthValid=m.fcValid=m.addressValid=m.statusValid=m.rateValid=true;
  m.frameControl=8;m.status=3;m.rateFlags=256;memcpy(m.transmitter,t.request.peer,6);memcpy(m.receiver,t.local,6);
  t.observeRxCoverage(m,epoch);assert(t.peerRxHt==1&&t.peerRxLastRate==256&&!t.peerRxOtherRate);
  m.rateFlags=0x0d;t.observeRxCoverage(m,epoch);assert(t.peerRxOtherRate==1&&t.peerRxLastRate==0x0d);
  m.status=0;t.observeRxCoverage(m,epoch);assert(t.peerRxOtherRate==1);
 }
 if(argc==2){std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
  assert(bytes.size()>40);ObservedPeerCapabilities actual;assert(actual.observe(bytes.data(),bytes.size(),bytes.data()+10,7,epoch));
  assert(actual.ht==0x016f&&actual.ampdu==31&&actual.mcs==0xffff);puts("PASS captured iPad HT TLV: 9-byte layout, HT 0x016f, A-MPDU 31, MCS 0xffff");}
 puts("PASS bounded PSF/MIF cadence, no catch-up periods, single completion, cancellation, generation/clock/deadline/budget stops; observed AWDL HT bounds, freshness, NSS/SMPS clamp, no invented WMM/aggregation");
}
