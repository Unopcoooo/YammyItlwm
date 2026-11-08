#include <cassert>
#include <cstdio>
#include <vector>
#include <cstring>
#include "contracts.hpp"
#include "ListenerDescriptor.hpp"
using namespace ItlIbss;
const uint8_t packet[]={0x33,0x33,0x00,0x00,0x00,0xfb,0x02,0x01,0x02,0x03,0x04,0x05,0x86,0xdd,0x60,0x00,0x00,0x00,0x00,0x2d,0x11,0xff,0xfe,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x12,0x34,0xff,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xfb,0x14,0xe9,0x14,0xe9,0x00,0x2d,0x4e,0x19,0x12,0x34,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x5f,0x61,0x69,0x72,0x64,0x72,0x6f,0x70,0x04,0x5f,0x74,0x63,0x70,0x05,0x6c,0x6f,0x63,0x61,0x6c,0x00,0x00,0x0c,0x00,0x01};

int main(){
 const uint8_t local[]={2,1,2,3,4,5};
 assert(ItlAwdl::mdnsMulticastEthernet(packet,sizeof(packet),local));
 assert(!ItlAwdl::mdnsMulticastEthernet(packet,sizeof(packet),nullptr));
 assert(!ItlAwdl::mdnsMulticastPayload(nullptr,packet+14,sizeof(packet)-14));
 assert(!ItlAwdl::mdnsMulticastPayload(packet,nullptr,sizeof(packet)-14));
 for(size_t n=0;n<sizeof(packet);++n)assert(!ItlAwdl::mdnsMulticastEthernet(packet,n,local));
 // Every IPv6/UDP/DNS mutation that changes the checksum is rejected.
 for(size_t i=14;i<sizeof(packet);++i){
  auto bad=std::vector<uint8_t>(packet,packet+sizeof(packet));bad[i]^=1;
  // Traffic class/flow label are outside the UDP checksum and admission policy.
  if(i<18)continue;
  assert(!ItlAwdl::mdnsMulticastEthernet(bad.data(),bad.size(),local));
 }
 for(size_t i: {size_t(0),size_t(6),size_t(12)}){
  auto bad=std::vector<uint8_t>(packet,packet+sizeof(packet));bad[i]^=1;
  assert(!ItlAwdl::mdnsMulticastEthernet(bad.data(),bad.size(),local));
 }
 uint8_t frame[ItlAwdl::kAwdlDataMax];size_t n=0;
 assert(ItlAwdl::encodeData(packet,sizeof(packet),local,0,7,frame,sizeof(frame),n)==ItlAwdl::DataError::Ok);
 assert(ItlAwdl::mdnsMulticastFrame(frame,n));
 for(size_t i=0;i<n;++i)assert(!ItlAwdl::mdnsMulticastFrame(frame,i));
 Reservation r={1,1,1,3,4,3,9,true};
 const uint64_t cmdAddress=0xc12345000ull,scratch=cmdAddress+sizeof(iwm_cmd_header)+offsetof(iwm_tx_cmd,scratch);
 Segment seg[2]={{0xa12345000ull,8},{0xb12345000ull,uint32_t(n-24-8)}};
 iwm_device_cmd cmd; iwm_tfd tfd;uint16_t sched=0;
 assert(mdnsDescriptor(r,frame,n,cmdAddress,scratch,seg,2,63,2,1,2000,cmd,tfd,sched));
 const auto tx=reinterpret_cast<iwm_tx_cmd*>(cmd.data);
 assert(cmd.hdr.qid==3&&cmd.hdr.idx==63&&tx->sta_id==3&&tx->tid_tspec==0);
 assert(tx->tx_flags==(IWM_TX_CMD_FLG_SEQ_CTL|(1<<IWM_TX_CMD_FLG_BT_PRIO_POS)));
 assert(tx->life_time==2000&&tx->len==n&&tfd.num_tbs==4);
 assert(sched==ItlAwdl::auxUnencryptedSchedulerEntry(uint16_t(n),3));
 assert(!memcmp(cmd.data+sizeof(*tx),frame,24));
 const auto expectedCmd=cmd;const auto expectedTfd=tfd;const auto expectedSched=sched;
 // Routing must change only qid when reusing the monitor builder.
 assert(listenerDescriptor(r,frame,n,cmdAddress,scratch,seg,2,63,2,1,2000,cmd,tfd,sched));
 assert(cmd.hdr.qid==2);cmd.hdr.qid=3;
 assert(!memcmp(&cmd,&expectedCmd,sizeof(cmd))&&!memcmp(&tfd,&expectedTfd,sizeof(tfd))&&sched==expectedSched);
 for(unsigned bad=0;bad<14;++bad){
  auto resources=r;auto segments=std::vector<Segment>(seg,seg+2);uint64_t ca=cmdAddress,sa=scratch;unsigned slot=63,antenna=2,priority=1,count=2,lifetime=2000;
  auto bytes=std::vector<uint8_t>(frame,frame+n);
  if(bad==0)resources.exclusivelyReserved=false;
  if(bad==1)resources.mcastSta=4;
  if(bad==2)resources.mcastQueue=2;
  if(bad==3)sa+=4;
  if(bad==4)segments[1].address=segments[0].address;
  if(bad==5)segments[1].address=cmdAddress;
  if(bad==6)segments[1].address=(1ull<<36)-1;
  if(bad==7)slot=256;
  if(bad==8)antenna=3;
  if(bad==9)priority=4;
  if(bad==10)count=0;
  if(bad==11)bytes[0]=0xd0;
  if(bad==12)lifetime=0;
  if(bad==13)lifetime=2001;
  memset(&cmd,0x5a,sizeof(cmd));memset(&tfd,0xa5,sizeof(tfd));sched=0xbeef;
  auto oldCmd=cmd;auto oldTfd=tfd;
  assert(!mdnsDescriptor(resources,bytes.data(),n,ca,sa,segments.data(),count,slot,antenna,priority,lifetime,cmd,tfd,sched));
  assert(!memcmp(&cmd,&oldCmd,sizeof(cmd))&&!memcmp(&tfd,&oldTfd,sizeof(tfd))&&sched==0xbeef);
 }
 puts("PASS mDNS admission, truncation/checksum mutations, AWDL codec, CAB/station/TID/no-ACK mapping, monitor compatibility, DMA/resource rejection and untouched outputs");
}
