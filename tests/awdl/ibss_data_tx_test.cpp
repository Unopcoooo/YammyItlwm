#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#define main peer_commands_original_main
#include "ibss_peer_test.cpp"
#undef main
#pragma clang diagnostic pop
#include "../../itlwm/awdl/IbssDataTx.hpp"
#include "../../itlwm/awdl/SingleDataProbe.hpp"
#include "old-management-descriptor.inc"
#include <functional>
struct DataTransport {
 struct Buffer {uint8_t storage[1540]={};const uint8_t *frame=nullptr;size_t length=0;uint64_t commandAddress=0x100000,scratchAddress=0x10000c;Segment segments[2];unsigned count=0;};
 PeerState &peer;Transport &pt;DataTransport(PeerState &p,Transport &t):peer(p),pt(t){}
 uint64_t clock=10000000000ULL;unsigned slot=3;unsigned gen=7,allocations=0,discards=0,reclaims=0,doorbells=0,stages=0,flushes=0;
 bool gate=true,allocated=false,failPrepare=false,healthy=true,stop=false,flushOK=true,failDescriptor=false;unsigned validations=0,failValidation=0;
 std::function<void()> preparation,validation;
 iwm_device_cmd command={};iwm_tfd tfd={};uint16_t scheduler=0;
 bool inGate(){return gate;}uint64_t now(){return clock;}uint32_t generation(){return gen;}
 bool snapshot(PeerTxContext &c){c.grant=peer.grant;c.slot=slot;c.now=clock;c.leaseEnd=clock+1000000000;c.antenna=1;c.priority=3;return healthy&&pt.drained;}
 bool prepareBuffer(const uint8_t *p,size_t n,Buffer &b){++allocations;allocated=true;memcpy(b.storage,p,n);b.frame=b.storage;b.length=failDescriptor?n-1:n;b.count=1;b.segments[0]={0x200000,uint32_t(n-24)};if(preparation)preparation();return !failPrepare;}
 bool revalidate(const PeerTxContext &c,uint64_t &time){time=clock;++validations;if(validation)validation();return validations!=failValidation&&healthy&&gen==c.grant.generation&&pt.drained;}
 void stage(const PeerTxContext &,Buffer &,const iwm_device_cmd &c,const iwm_tfd &d,uint16_t s){++stages;command=c;tfd=d;scheduler=s;pt.drained=false;}
 void doorbell(const PeerTxContext &){++doorbells;}
 void discardBuffer(Buffer &){assert(allocated);allocated=false;++discards;}
 void reclaim(const PeerTxContext &,Buffer &){assert(allocated);allocated=false;++reclaims;pt.drained=true;}
 bool flush(uint32_t mask){assert(mask==(1u<<10));++flushes;return flushOK;}
 bool stopProof(const PeerTxContext &c){return stop&&c.grant.generation==7;}
};
static std::vector<uint8_t> frame(const PeerGrant &g){
 ItlAwdl::SingleDataRequest r;r.token=123;r.sourceIp[0]=r.peerIp[0]=0xfe;r.sourceIp[1]=r.peerIp[1]=0x80;r.sourceIp[15]=1;r.peerIp[15]=2;memcpy(r.peer,g.peer,6);
 std::vector<uint8_t> f(104);size_t n;assert(ItlAwdl::buildSingleDataEcho(r,g.local,g.generation,3,f.data(),f.size(),n)&&n==104);return f;
}
#ifndef ITL_IBSS_DATA_TEST_MAIN
#define ITL_IBSS_DATA_TEST_MAIN main
#endif
int ITL_IBSS_DATA_TEST_MAIN(){
 PeerState s;Transport pt;pt.state=&s;PeerCommands<Transport> pc(pt,s);assert(!pc.create(grant()));auto f=frame(s.grant);
 iwm_device_cmd command,oldCommand;iwm_tfd tfd,oldTfd;uint16_t scheduler,oldScheduler;
 Segment seg={0x200000,80};assert(peerDataDescriptor(s,f.data(),f.size(),0x100000,0x10000c,&seg,1,3,1,3,command,tfd,scheduler));
 const auto tx=reinterpret_cast<const iwm_tx_cmd*>(command.data);
 assert(command.hdr.qid==10&&command.hdr.idx==3&&tx->sta_id==5&&!tx->tid_tspec&&tx->len==104&&
  tx->data_retry_limit==kPeerDataRetryLimit&&tx->rts_retry_limit==kPeerRtsRetryLimit);
 assert((tx->tx_flags&IWM_TX_CMD_FLG_ACK)&&!tx->sec_ctl&&tfd.num_tbs==3&&tfd.tbs[2].hi_n_len==(80<<4)&&scheduler==0x501c);
 oldCommand=command;oldTfd=tfd;oldScheduler=scheduler;
 for(bool badEntry:{false,true}){PeerState peer;Transport t;t.state=&peer;PeerCommands<Transport> p(t,peer);assert(!p.create(grant()));
  DataTransport d{peer,t};PeerDataTx<DataTransport> data(d,peer);auto bytes=frame(peer.grant);d.healthy=false;
  assert(!data.start(badEntry?nullptr:bytes.data(),bytes.size())&&!d.allocations&&!d.doorbells);
  assert(data.rejectionCode()==(badEntry?1u:2u));
 }
 for(unsigned fault=0;fault<7;++fault){auto p=s;auto bytes=f;auto segment=seg;uint64_t scratch=0x10000c;
  if(fault==0)p.ready=false;if(fault==1)bytes[4]^=2;if(fault==2)bytes[10]^=2;if(fault==3)bytes[0]=0xd0;
  if(fault==4)segment.address=0x100010;if(fault==5)segment.length=79;if(fault==6)scratch++;
  assert(!peerDataDescriptor(p,bytes.data(),bytes.size(),0x100000,scratch,&segment,1,3,1,3,command,tfd,scheduler));
  assert(!memcmp(&oldCommand,&command,sizeof(command))&&!memcmp(&oldTfd,&tfd,sizeof(tfd))&&oldScheduler==scheduler);
 }
 // MTU-sized payload, scatter DMA, high DQA queue bit and ring wrap boundary.
 auto wide=s;wide.grant.queue=31;std::vector<uint8_t> eth(1514),wire(1540);memcpy(eth.data(),wide.grant.peer,6);memcpy(eth.data()+6,wide.grant.local,6);
 eth[12]=0x86;eth[13]=0xdd;eth[14]=0x60;eth[18]=1460>>8;eth[19]=1460&255;eth[20]=59;eth[21]=64;eth[22]=eth[38]=0xfe;
 size_t wireSize;assert(ItlAwdl::encodeData(eth.data(),eth.size(),wide.grant.local,4095,65535,wire.data(),wire.size(),wireSize)==ItlAwdl::DataError::Ok);
 Segment scatter[2]={{0x200000,1000},{0x300000,516}};
 assert(peerDataDescriptor(wide,wire.data(),wireSize,0x100000,0x10000c,scatter,2,255,1,3,command,tfd,scheduler)&&command.hdr.qid==31&&command.hdr.idx==255&&tfd.num_tbs==4);
 // Refactoring common DMA checks preserves the real old management builder byte-for-byte.
 uint8_t action[40];assert(ItlAwdl::buildAuxTxProbe(s.grant.local,100,action));Segment body={0x200000,16};
 assert(descriptor(s.grant.base,action,40,0x100000,0x10000c,&body,1,3,1,3,2000,command,tfd,scheduler));
 assert(descriptorBefore(s.grant.base,action,40,0x100000,0x10000c,&body,1,3,1,3,2000,oldCommand,oldTfd,oldScheduler));
 assert(!memcmp(&command,&oldCommand,sizeof(command))&&!memcmp(&tfd,&oldTfd,sizeof(tfd))&&scheduler==oldScheduler);
 for(unsigned status:{1u,2u,4u}){
  PeerState peer;Transport t;t.state=&peer;PeerCommands<Transport> p(t,peer);assert(!p.create(grant()));
  DataTransport d{peer,t};PeerDataTx<DataTransport> data(d,peer);auto bytes=frame(peer.grant);assert(data.start(bytes.data(),bytes.size())&&data.pending()&&d.allocated&&d.doorbells==1);
  assert(!data.start(bytes.data(),bytes.size()));assert(data.complete(7,9,3,status)==decltype(data)::Completion::Unowned);
  d.gate=false;assert(data.complete(7,10,3,status)==decltype(data)::Completion::Defer&&d.allocated);d.gate=true;
  assert(data.complete(8,10,3,status)==decltype(data)::Completion::Consume&&d.allocated);
  data.cancel();assert(d.flushes==1&&peer.flushSent&&d.allocated&&data.pending());
  assert(p.remove()==EBUSY&&!t.flushes&&!t.releases&&d.allocated);
  assert(data.complete(7,10,3,status)==decltype(data)::Completion::Reclaim&&!d.allocated&&d.reclaims==1&&data.ackReported()==(status==1));
  assert(data.complete(7,10,3,status)==decltype(data)::Completion::Consume&&d.reclaims==1);
  assert(!p.remove()&&t.releases==1&&!data.hardwareStopped());
 }
 for(unsigned fault=0;fault<7;++fault){PeerState peer;Transport t;t.state=&peer;PeerCommands<Transport> p(t,peer);assert(!p.create(grant()));
  DataTransport d{peer,t};PeerDataTx<DataTransport> data(d,peer);auto bytes=frame(peer.grant);
  if(fault==0)d.failPrepare=true;if(fault==1)d.failValidation=1;if(fault==2)d.failValidation=2;
  if(fault==3)d.preparation=[&](){PeerDataTx<DataTransport> other(d,peer);assert(!other.start(bytes.data(),bytes.size()));data.cancel();};if(fault==4)d.preparation=[&](){peer.grant.color++;};
  if(fault==6)d.failDescriptor=true;
  if(fault==5)d.preparation=[&](){d.clock+=2000000000;};
  assert(!data.start(bytes.data(),bytes.size())&&!d.doorbells&&!d.allocated&&d.discards==1);
  const unsigned stages[]={3,4,6,4,4,4,5};assert(data.rejectionCode()==stages[fault]);
 }
 for(unsigned fault=0;fault<4;++fault){PeerState peer;Transport t;t.state=&peer;PeerCommands<Transport> p(t,peer);assert(!p.create(grant()));
  DataTransport d{peer,t};PeerDataTx<DataTransport> data(d,peer);auto bytes=frame(peer.grant);
  d.validation=[&](){if(d.validations==2){if(fault==0)data.cancel();if(fault==1)peer.grant.color++;if(fault==2)d.gen++;if(fault==3)d.gate=false;}};
  assert(!data.start(bytes.data(),bytes.size())&&!d.doorbells&&!d.allocated&&d.discards==1);
 }
 for(bool generationChange:{false,true}){PeerState peer;Transport t;t.state=&peer;PeerCommands<Transport> p(t,peer);assert(!p.create(grant()));
  DataTransport d{peer,t};PeerDataTx<DataTransport> data(d,peer);auto bytes=frame(peer.grant);assert(data.start(bytes.data(),bytes.size()));
  if(generationChange)d.gen++;else d.clock+=2000000000ULL;data.tick();data.tick();
  assert(data.pending()&&d.allocated&&d.flushes==(generationChange?0u:1u));d.stop=true;assert(data.hardwareStopped()&&!d.allocated&&d.reclaims==1);
 }
 PeerState peer;Transport t;t.state=&peer;PeerCommands<Transport> p(t,peer);assert(!p.create(grant()));
 DataTransport d{peer,t};PeerDataTx<DataTransport> data(d,peer);f=frame(peer.grant);assert(data.start(f.data(),f.size()));
 d.flushOK=false;data.cancel();assert(peer.unknown&&d.allocated&&data.pending()&&p.remove()==EIO&&!t.releases);
 assert(!data.hardwareStopped()&&d.allocated);d.stop=true;assert(data.hardwareStopped()&&!d.allocated&&!data.hardwareStopped());
 t.stopped=true;assert(p.hardwareStopped()&&t.releases==1);
 puts("PASS IBSS data core: real ACK/peer queue descriptor and DMA, management-byte regression, Echo frame to submission/completion, SUCCESS vs DIRECT_DONE, shared flush ownership, no early peer deletion, duplicate/wrong completion, late/cancel/preparation rejection, failed flush waits real hardware stop. No live Transport.");
}
