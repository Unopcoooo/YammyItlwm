#include <cassert>
#include <cstdio>
#include <vector>
#include <fstream>
#include "FixedChannelControl.hpp"
#include "LinkIdentityAdvertisement.hpp"
using namespace ItlAwdl;
static const uint8_t own[6]={2,0,0,0,0,1},a[6]={2,0,0,0,0,2},b[6]={2,0,0,0,0,3},top[6]={2,0,0,0,0,9};
static std::vector<uint8_t> frame(const uint8_t *mac,uint32_t metric=541,uint32_t counter=100){
 SelfControlInput i;memcpy(i.source,mac,6);i.subtype=3;i.generation=i.currentGeneration=6;
 i.epochHostNs=i.nowHostNs=1000000000ULL;i.epochGp2=i.nowGp2=100000;i.leaseEndNs=10000000000ULL;
 i.selfMetric=metric;i.advertisementCounter=counter;
 std::vector<uint8_t> f(kLinkIdentityLength);size_t n;assert(buildLinkIdentity(i,f.data(),f.size(),n));return f;
}
static void election(std::vector<uint8_t> &f,const uint8_t *master,const uint8_t *sync,uint32_t height){
 memcpy(f.data()+43+21,master,6);memcpy(f.data()+187,master,6);memcpy(f.data()+193,sync,6);actionWrite32(f.data()+203,height);
}
static FixedControlView get(FixedChannelControl &n,uint32_t gp2=100000,uint64_t host=1000000000ULL){FixedControlView v;assert(n.view(6,gp2,host,v));return v;}
int main(int argc,char **argv){
 FixedChannelControl n;FixedControlView v;assert(n.reset(own,6));auto f=frame(a);
 // A PSF with a version does not qualify until its MIF arrives.
 f[30]=0;assert(n.observe(f.data(),f.size(),6,100000,1000000000));assert(!n.view(6,100000,1000000000,v));
 f[30]=3;assert(n.observe(f.data(),f.size(),6,100000,1000000000));v=get(n);
 assert(peerSameAddress(v.election.masterAddress,a)&&v.election.height==1&&v.aw==0&&v.down==64);
 // Metric-first differs from the old counter-first model.
 auto q=frame(b,600,1);assert(n.observe(q.data(),q.size(),6,100000,1000000000));v=get(n);assert(peerSameAddress(v.source,b));
 election(q,top,b,2);assert(n.observe(q.data(),q.size(),6,100000,1000000000));v=get(n);
 assert(peerSameAddress(v.election.masterAddress,top)&&peerSameAddress(v.election.syncAddress,b)&&v.election.height==3);
 election(q,top,own,0);assert(n.observe(q.data(),q.size(),6,100000,1000000000));assert(peerSameAddress(get(n).source,a));
 election(q,top,b,0xffffffff);assert(n.observe(q.data(),q.size(),6,100000,1000000000));assert(peerSameAddress(get(n).source,a));
 assert(!n.observe(f.data(),f.size(),7,100000,1000000000));assert(!n.observe(f.data(),f.size()-1,6,100000,1000000000));
 assert(!n.view(6,600001,1500001000,v));assert(!n.view(6,1,999999999,v));
 // Version-zero MIF never qualifies; reset cannot reuse previous admission.
 assert(n.reset(own,7));f.back()=0;assert(n.observe(f.data(),f.size(),7,100000,1000000000));assert(!n.view(7,100000,1000000000,v));
 assert(n.reset(own,6));f=frame(a);actionWrite16(f.data()+72,65535);actionWrite16(f.data()+44,1);
 assert(n.observe(f.data(),f.size(),6,0xffffff00,1000000000));v=get(n,768,1001024000);assert(v.aw==0&&v.down==64);
 uint8_t wire[227];size_t used;assert(n.build(6,768,1001024000,3,2,wire,sizeof(wire),used,v)&&used==227);
 Advertisement decoded;assert(decodeAdvertisement(wire,used,decoded)==AdvertisementResult::Valid);
 assert(peerSameAddress(decoded.sender,own)&&peerSameAddress(decoded.election.masterAddress,a)&&decoded.election.height==1);
 for(auto c:decoded.channels.entries)assert(c.channel==6&&c.auxiliary==81);
 // A stale or wrong-generation snapshot cannot leave a sendable output.
 used=227;assert(!n.build(7,768,1001024000,3,2,wire,sizeof(wire),used,v)&&used==0);
 if(argc==2){
  std::ifstream in(argv[1],std::ios::binary);assert(in);uint32_t count;in.read(reinterpret_cast<char*>(&count),4);assert(count&&count<10000);
  assert(n.reset(own,6));unsigned qualified=0;
  for(unsigned i=0;i<count;++i){uint32_t size,gp2;uint64_t host;uint16_t aw,down;uint8_t expected;
   in.read(reinterpret_cast<char*>(&size),4);in.read(reinterpret_cast<char*>(&host),8);in.read(reinterpret_cast<char*>(&gp2),4);
   in.read(reinterpret_cast<char*>(&aw),2);in.read(reinterpret_cast<char*>(&down),2);in.read(reinterpret_cast<char*>(&expected),1);
   assert(size<=4096);std::vector<uint8_t> bytes(size);in.read(reinterpret_cast<char*>(bytes.data()),size);assert(in);
   assert(n.observe(bytes.data(),size,6,gp2,host));bool ready=n.view(6,gp2,host,v);assert(ready==bool(expected));
   if(ready){++qualified;assert(v.aw==aw&&v.down==down);assert(n.build(6,gp2,host,3,0,wire,sizeof(wire),used,v));
    assert(decodeAdvertisement(wire,used,decoded)==AdvertisementResult::Valid);
    assert(peerSameAddress(decoded.election.syncAddress,bytes.data()+10)&&decoded.sync.nextAwSequence==aw&&decoded.sync.txDownCounter==down);
   }
  }
  printf("PASS real capture replay: %u control frames, %u admitted snapshots\n",count,qualified);
 }
 puts("PASS fixed-channel metric election, MIF/version admission, relay vs master, cycle/height rejection, generation/time guards, GP2/AW wrap and truthful control encoding");
}
