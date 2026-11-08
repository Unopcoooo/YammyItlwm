#include "../../itlwm/awdl/AuxRoc.hpp"
#include <cassert>
#include <cstdio>
using namespace ItlAwdl;
int main(){
 uint32_t channel;
 for(unsigned i=0;i<24;++i){assert(rxControlChannel(36,i,channel));assert(channel==((i/4)%2?6:36));}
 assert(!rxControlChannel(6,0,channel)&&!rxControlChannel(36,24,channel)&&!rxControlChannel(100,0,channel));
 uint8_t mac[6]={2,1,2,3,4,5};AuxRocCommand cmd;
 assert(!buildAuxRocAdd(false,36,1,mac,0x12345678,64,300,cmd));
 assert(buildAuxRocAdd(false,36,1,mac,0x12345678,64,300,cmd,36));
 assert(cmd.length==40 && cmd.bytes[16]==0&&cmd.bytes[17]==36&&rocRead32(cmd.bytes+28)==0x12345678&&rocRead32(cmd.bytes+36)==64);
 assert(buildAuxRocAdd(true,11,1,mac,7,64,300,cmd,11));
 assert(cmd.length==44&&rocRead32(cmd.bytes+16)==11&&cmd.bytes[20]==1&&rocRead32(cmd.bytes+32)==7);
 assert(!buildAuxRocAdd(true,100,1,mac,0,64,300,cmd,100));
 assert(!buildAuxRocAdd(true,36,1,mac,0,64,300,cmd,40));
 ReceiveWindow w;assert(!w.contains(1,36,100));w={1,36,0xfffffff0u,16,1024,true,true};
 assert(w.contains(1,36,0)&&!w.contains(1,36,16)&&!w.contains(2,36,0)&&!w.contains(1,6,0));
 w.end=w.start;assert(!w.contains(1,36,w.start));w.end=w.start+2048;assert(!w.contains(1,36,w.start));
 puts("PASS: home/social balanced 24-request policy, strict default channel gate, explicit validated-home wire encoding and post-END GP2 retention bounds");
}
