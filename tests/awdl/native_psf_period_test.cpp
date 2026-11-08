#include <cassert>
#include <cstring>
#include <initializer_list>
#include "BoundedControlSchedule.hpp"
#include "SelfControlAdvertisement.hpp"
using S=ItlAwdl::BoundedControlSchedule;
int main(){
 S s;const uint64_t t=1000000000,end=t+10000000000ULL;
 assert(!s.setPsfPeriod(7,t,440));assert(s.arm(7,t,end,64));
 assert(s.publish(S::Psf,7,t,64));const auto mif=s.nextMif;
 assert(s.setPsfPeriod(7,t+1,440)&&s.pending&&s.inFlight==S::Psf&&s.submitted==1&&s.completed==0);
 assert(s.nextPsf==t+440*S::Tu&&s.nextMif==mif&&s.endNs==end);
 assert(s.due(7,t+1,true)==S::None);assert(s.complete(true));
 const auto before=s;
 for(auto period:{0u,109u,111u,439u,441u,65536u}){
  assert(!s.setPsfPeriod(7,t+2,period));assert(!memcmp(&s,&before,sizeof(s)));
 }
 assert(!s.setPsfPeriod(8,t+2,110)&&!s.setPsfPeriod(7,t,110)&&!s.setPsfPeriod(7,end,110));
 assert(s.setPsfPeriod(7,t+2,110)&&s.nextPsf==t+110*S::Tu);
 assert(s.setPsfPeriod(7,t+500*S::Tu,440)); // A late switch sends one due PSF.
 assert(s.due(7,t+500*S::Tu,true)==S::Mif);
 assert(s.publish(S::Mif,7,t+500*S::Tu,64)&&s.complete(true));
 assert(s.due(7,t+500*S::Tu,true)==S::Psf);
 assert(s.publish(S::Psf,7,t+500*S::Tu,64));assert(s.nextPsf==t+880*S::Tu);
 assert(s.complete(true));s.stop();const auto stopped=s;
 assert(!s.setPsfPeriod(7,t+501*S::Tu,110)&&!memcmp(&s,&stopped,sizeof(s)));
 // A shorter remaining lease never overflows or extends the deadline.
 S shortLease;assert(shortLease.arm(7,t,t+200*S::Tu,64));assert(shortLease.publish(S::Psf,7,t,64));
 assert(shortLease.setPsfPeriod(7,t+1,440)&&shortLease.nextPsf==shortLease.endNs);
 // Unchanged default produces identical bytes; only AF period changes in440 frames.
 ItlAwdl::SelfControlInput i;i.source[0]=2;i.source[5]=1;i.generation=i.currentGeneration=7;
 i.epochHostNs=i.nowHostNs=t;i.epochGp2=i.nowGp2=1000;i.leaseEndNs=end;
 uint8_t a[512]={},b[512]={};size_t an=0,bn=0;ItlAwdl::SelfControlEvidence ae,be;
 assert(ItlAwdl::buildSelfControlAdvertisement(i,a,sizeof(a),an,ae));
 i.advertisementPeriod=440;assert(ItlAwdl::buildSelfControlAdvertisement(i,b,sizeof(b),bn,be));assert(an==bn);
 assert(a[50]==110&&a[51]==0&&b[50]==0xb8&&b[51]==1);
 for(size_t n=0;n<an;++n)if(n!=50&&n!=51)assert(a[n]==b[n]);
 i.advertisementPeriod=65536;assert(!ItlAwdl::buildSelfControlAdvertisement(i,b,sizeof(b),bn,be)&&!bn);
}
