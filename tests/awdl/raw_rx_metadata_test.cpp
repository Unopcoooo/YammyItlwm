#include "../../itlwm/awdl/RawRxMetadata.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace ItlAwdl;
int main(){
 for(unsigned n=0;n<48;++n){std::vector<uint8_t> p(n);auto m=decodeRawRx(p.data(),n,true);assert(!m.descriptorValid);}
 uint8_t mq[80]={};mq[0]=24;mq[12]=3;mq[34]=149;mq[35]=4;mq[7]=9;
 mq[24]=0x78;mq[25]=0x56;mq[26]=0x34;mq[27]=0x12;
 mq[36]=0xf0;mq[37]=0xff;mq[38]=0xff;mq[39]=0xff;mq[48]=0x80;
 auto m=decodeRawRx(mq,72,true);
 assert(m.descriptorValid&&m.lengthValid&&m.statusValid&&m.fcValid&&m.channel==149&&m.gp2==0xfffffff0);
 assert(m.macContext==4&&m.macPhyIndex==9&&m.filterMatch==0x12345678&&m.frameControl==0x80);
 assert(!decodeRawRx(mq,60,true).lengthValid&&!decodeRawRx(mq,60,true).fcValid);
 mq[0]=1;assert(!decodeRawRx(mq,49,true).fcValid);
 uint8_t legacy[32]={};legacy[0]=24;legacy[4]=0xd0;legacy[28]=3;
 m=decodeRawRx(legacy,32,false,44,1234);
 assert(m.descriptorValid&&m.lengthValid&&m.statusValid&&m.fcValid&&m.status==3&&m.channel==44&&m.gp2==1234);
 assert(!decodeRawRx(legacy,31,false,44,1234).statusValid);
 assert(!decodeRawRx(nullptr,80,true).descriptorValid);
 // Exact-sized allocations make the ASan boundary check meaningful.
 for(unsigned n=0;n<100;++n)for(unsigned len=0;len<100;++len){
  std::vector<uint8_t> p(n,0);if(n>=2){p[0]=uint8_t(len);p[1]=0;}
  for(bool mqMode:{false,true})(void)decodeRawRx(p.data(),p.size(),mqMode,165,1234);
 }
 puts("raw RX metadata: exact wire bounds, MQ/legacy offsets, short headers/body/status, no FC on truncation and byte order PASS");
}
