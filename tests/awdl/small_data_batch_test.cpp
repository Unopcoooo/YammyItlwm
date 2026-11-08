#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#define ITL_IBSS_DATA_TEST_MAIN old_data_test
#include "ibss_data_tx_test.cpp"
#pragma clang diagnostic pop
#include "../../itlwm/awdl/SmallDataBatch.hpp"
#include "../../itlwm/awdl/IbssDataTrial.hpp"
static std::vector<uint8_t> replyFrame(const ItlAwdl::SingleDataRequest &r,const uint8_t *local,unsigned gen){
 uint8_t request[104],eth[78],wire[106];size_t n;uint16_t seq;
 assert(ItlAwdl::buildSingleDataEcho(r,local,gen,1,request,sizeof(request),n));
 assert(ItlAwdl::decodeData(request,n,r.peer,local,eth,sizeof(eth),n,seq)==ItlAwdl::DataError::Ok);
 memcpy(eth,local,6);memcpy(eth+6,r.peer,6);auto ip=eth+14;
 memcpy(ip+8,r.peerIp,16);memcpy(ip+24,r.sourceIp,16);ip[40]=129;ip[42]=ip[43]=0;
 const auto sum=ItlAwdl::probeChecksum(ip);ip[42]=sum>>8;ip[43]=sum;
 assert(ItlAwdl::encodeData(eth,sizeof(eth),r.peer,1,1,wire,sizeof(wire),n)==ItlAwdl::DataError::Ok);
 return std::vector<uint8_t>(wire,wire+n);
}
int main(){
 using ItlAwdl::SmallDataBatch;
 assert(SmallDataBatch::tokenAt(0,0)==0&&SmallDataBatch::tokenAt(1,3)==0);
 assert(SmallDataBatch::tokenAt(0x7fffffffffffffffULL,1)==1);
 for(unsigned fault=0;fault<8;++fault){
  PeerState peer;Transport pt;pt.state=&peer;PeerCommands<Transport> commands(pt,peer);assert(!commands.create(grant()));
  DataTransport t(peer,pt);PeerDataTx<DataTransport> data(t,peer);auto bytes=frame(peer.grant);
  assert(!data.rearmAfterAck());assert(data.start(bytes.data(),bytes.size()));
  assert(!data.rearmAfterAck());
  if(fault!=0)assert(data.complete(7,10,3,1)==decltype(data)::Completion::Reclaim);
  if(fault==1)t.gate=false;if(fault==2)t.gen++;if(fault==3)peer.unknown=true;
  if(fault==4)data.cancel();if(fault==5)peer.flushSent=true;if(fault==6)t.clock+=2000000000ULL;
  if(fault==7)peer.grant.color++;
  assert(!data.rearmAfterAck());
 }
 PeerState peer;Transport pt;pt.state=&peer;PeerCommands<Transport> commands(pt,peer);assert(!commands.create(grant()));
 DataTransport transport(peer,pt);PeerDataTx<DataTransport> data(transport,peer);
 ItlIbss::DataTrial trial;trial.smallBatch.enabled=true;trial.generation=7;
 trial.request.token=0x7fffffffffffffffULL;memcpy(trial.local,peer.grant.local,6);memcpy(trial.request.peer,peer.grant.peer,6);
 trial.request.sourceIp[0]=trial.request.peerIp[0]=0xfe;trial.request.sourceIp[1]=trial.request.peerIp[1]=0x80;
 trial.request.sourceIp[15]=1;trial.request.peerIp[15]=2;
 uint64_t now=10000000000ULL;const auto end=now+2500000000ULL;
 std::vector<uint8_t> firstReply;
 for(unsigned i=0;i<3;++i){
  assert(trial.smallBatch.index==i);auto r=trial.currentRequest();
  uint8_t wire[104];size_t n;assert(ItlAwdl::buildSingleDataEcho(r,trial.local,7,i+1,wire,sizeof(wire),n));
  transport.slot=3+i;assert(data.start(wire,n));trial.phase=ItlIbss::DataTrialPhase::Submitted;trial.sentNs=now;
  assert(trial.beginReply(100000+i*100000,end));
  assert(trial.replyEndNs==end&&!trial.smallBatch.mayAdvance(now));
  if(i)assert(data.complete(7,10,2+i,1)==decltype(data)::Completion::Unowned&&data.pending());
  assert(data.complete(7,10,3+i,1)==decltype(data)::Completion::Reclaim);
  assert(!transport.allocated&&trial.smallBatch.markCompleted()&&!trial.smallBatch.markCompleted());
  if(i)assert(!trial.observeReply(firstReply.data(),firstReply.size(),7,6,100001+i*100000,now+1000));
  auto reply=replyFrame(r,trial.local,7);if(!i)firstReply=reply;
  assert(trial.observeReply(reply.data(),reply.size(),7,6,100001+i*100000,now+1000));
  assert(!trial.observeReply(reply.data(),reply.size(),7,6,100002+i*100000,now+2000));
  if(i<2){assert(trial.smallBatch.mayAdvance(now));assert(data.rearmAfterAck()&&!data.rearmAfterAck());
   assert(trial.smallBatch.advance(now));now+=100000000ULL;transport.clock=now;
  }else assert(!trial.smallBatch.mayAdvance(now)&&!data.rearmAfterAck());
 }
 assert(trial.smallBatch.success()&&transport.doorbells==3&&transport.reclaims==3&&peer.ready&&!peer.released);
 trial.completed=trial.ack=true;trial.result=ItlIbss::DataTrialResult::AckReported;trial.finishRecovery(true);
 assert(trial.phase==ItlIbss::DataTrialPhase::Complete);
 trial.smallBatch.replied=3;trial.finishRecovery(true);assert(trial.phase==ItlIbss::DataTrialPhase::Failed);
 auto b=trial.smallBatch;assert(!b.advance(end));
 puts("PASS three packets / one peer: drained rearm, cap3, unique/wrapped tokens, actual Echo wire decoding, stale reply and old-slot completion rejection, no early peer deletion, fixed end, no false batch success; ASan/UBSan, no RF");
}
