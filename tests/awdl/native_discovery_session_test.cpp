#include <cassert>
#include <cstdio>
#include <vector>
#include "NativeDiscoverySession.hpp"
#include "ProtocolModel.hpp"
using namespace ItlAwdl;
const uint8_t packet[]={51,51,0,0,0,251,2,1,2,3,4,5,134,221,96,0,0,0,0,45,17,255,254,128,0,0,0,0,0,0,0,0,0,0,0,0,18,52,255,2,0,0,0,0,0,0,0,0,0,0,0,0,0,251,20,233,20,233,0,45,78,25,18,52,0,0,0,1,0,0,0,0,0,0,8,95,97,105,114,100,114,111,112,4,95,116,99,112,5,108,111,99,97,108,0,0,12,0,1};

int main(){
 const uint8_t local[]={2,1,2,3,4,5},remote[]={2,2,3,4,5,6};
 NativeDiscoverySession s;s.removeDuplicatesRx=true;NativeDiscoverySession::Packet out;
 assert(!s.begin(0,1,1,local,1,20000000001ULL));
 assert(!s.begin(1,0,1,local,1,20000000001ULL));
 assert(!s.begin(1,1,0,local,1,20000000001ULL));
 assert(!s.begin(1,1,1,local,1,20000000002ULL));
 assert(s.begin(1,1,1,local,1,20000000001ULL));
 assert(!s.begin(1,1,1,local,1,20000000001ULL));
 assert(!s.offer(packet,sizeof(packet),2));s.open=true;
 assert(!s.matches(2,1,1,2)&&!s.matches(1,2,1,2)&&!s.matches(1,1,2,2));
 for(unsigned round=0;round<80;++round){
  const unsigned count=NativeDiscoverySession::MaxQueued;
  for(unsigned i=0;i<count;++i)assert(s.offer(packet,sizeof(packet),100));
  assert(!s.offer(packet,sizeof(packet),100));
  for(unsigned i=0;i<count;++i){assert(s.takeTx(101,out));assert(!memcmp(packet,out.bytes,sizeof(packet)));++s.submitted;}
 }
 assert(s.submitted==80*NativeDiscoverySession::MaxQueued&&s.traceOverflow>0);
 assert(s.offer(packet,sizeof(packet),100));assert(s.takeTx(101,out));
 for(unsigned i=0;i<NativeDiscoverySession::MaxQueued;++i)assert(s.offer(packet,sizeof(packet),101));
 assert(s.txCount==NativeDiscoverySession::MaxQueued);
 assert(s.retryFront(out.bytes,out.length,0,0,102)&&s.txCount==NativeDiscoverySession::QueueCapacity&&s.peerSoftwareRetries==1);
 assert(s.takeTx(103,out)&&out.softwareRetries==1&&s.txCount==NativeDiscoverySession::MaxQueued);
 while(s.txCount)assert(s.takeTx(103,out));
 for(unsigned i=0;i<NativeDiscoverySession::MaxQueued;++i)assert(s.offer(packet,sizeof(packet),103));
 assert(!s.offer(packet,sizeof(packet),103));
 assert(s.retryFront(out.bytes,out.length,NativeDiscoverySession::MaxSoftwareRetries-1,0,104)&&s.txCount==NativeDiscoverySession::QueueCapacity);
 assert(!s.retryFront(out.bytes,out.length,NativeDiscoverySession::MaxSoftwareRetries,0,105));
 assert(s.takeTx(106,out)&&out.softwareRetries==NativeDiscoverySession::MaxSoftwareRetries&&s.txCount==NativeDiscoverySession::MaxQueued);
 while(s.txCount)assert(s.takeTx(106,out));
 auto eth=std::vector<uint8_t>(packet,packet+sizeof(packet));memcpy(eth.data()+6,remote,6);
 uint8_t frame[kAwdlDataMax];size_t n=0;
 assert(encodeData(eth.data(),eth.size(),remote,0,4095,frame,sizeof(frame),n)==DataError::Ok);
 assert(!s.receive(frame,n,100,1,false));assert(!s.receive(frame,n,100,0,true));
 assert(s.receive(frame,n,100,1,true));assert(!s.receive(frame,n,100,1,true));
 assert(s.takeRx(101,1,out)&&out.length==eth.size()&&!memcmp(out.bytes,eth.data(),out.length));
 assert(encodeData(eth.data(),eth.size(),remote,1,0,frame,sizeof(frame),n)==DataError::Ok);
 assert(s.receive(frame,n,102,1,true));assert(!s.takeRx(103,2,out));
 assert(encodeData(eth.data(),eth.size(),remote,1,1,frame,sizeof(frame),n)==DataError::Ok);
 assert(s.receive(frame,n,104,1,true));assert(!s.takeRx(500000105ULL,1,out));
 assert(encodeData(eth.data(),eth.size(),remote,1,2,frame,sizeof(frame),n)==DataError::Ok);
 assert(s.receive(frame,n,104,1,true));s.close();assert(!s.takeRx(105,1,out)&&!s.offer(packet,sizeof(packet),105));
 assert(!s.receive(frame,n,105,1,true)&&!s.open&&s.closed&&!s.matches(1,1,1,105));
 assert(s.counts[s.StalePeer]==2&&s.counts[s.Sequence]==1);
 NativeDiscoverySession trace;trace.observe(true,8);assert(!trace.counts[trace.RawData]);trace.open=true;
 for(unsigned i=0;i<5000;++i){trace.observe(true,0x4208);trace.observe(true,0x88);trace.observe(false,0x88,false);trace.observe(false,0xb4);}
 assert(trace.counts[trace.RawData]==10000&&trace.counts[trace.RawPlain]==5000&&trace.counts[trace.CommonPlain]==5000&&trace.counts[trace.CommonChained]==5000);
 trace.counts[trace.RawData]=UINT32_MAX;trace.observe(true,8);assert(trace.counts[trace.RawData]==UINT32_MAX);
 NativeDiscoverySession expiry;assert(expiry.begin(1,1,1,local,1,5000000000ULL));expiry.open=true;
 assert(expiry.offer(packet,sizeof(packet),2));
 assert(expiry.takeTx(2+NativeDiscoverySession::QueuedLifetimeNs/2,out));
 assert(expiry.offer(packet,sizeof(packet),2));
 assert(!expiry.takeTx(2+NativeDiscoverySession::QueuedLifetimeNs+1,out));
 // Exhaust all microsecond phases of an advertised 16-EAW supercycle.
 unsigned windows=0;for(uint32_t us=0;us<16*NativeSyncProfile::periodUs;++us){
  uint64_t close=0;const bool allowed=NativeDiscoverySession::window(us,1000000000,21000000000ULL,close);
  const auto slot=us/NativeSyncProfile::periodUs,phase=us%NativeSyncProfile::periodUs;
  const bool expected=(slot==0||slot==10)&&phase>=18432&&phase+2000<47104;
  assert(allowed==expected);if(allowed){++windows;assert(close>1002000000&&close<=1028672000ULL);}
 }
 assert(windows==2*(47104-18432-2000));
 uint64_t close=0;assert(!NativeDiscoverySession::window(20000,1000000000,1001000000,close));
 assert(!NativeDiscoverySession::window(20000,UINT64_MAX-1000000,UINT64_MAX,close));
 NativePeerKey learned;memcpy(learned.mac,remote,6);
 assert(nativePeerIpv6FromMac(learned.mac,learned.ip));
 const uint8_t iphoneMac[6]={0x82,0x6e,0x99,0x77,0x07,0x61};
 const uint8_t iphoneIp[16]={0xfe,0x80,0,0,0,0,0,0,0x80,0x6e,0x99,0xff,0xfe,0x77,0x07,0x61};
 uint8_t derived[16];assert(nativePeerIpv6FromMac(iphoneMac,derived)&&!memcmp(derived,iphoneIp,16));
 learned.generation=2;learned.epoch=3;
 uint8_t probe[kAwdlDataMax];size_t probeLength=0;
 assert(nativePeerProbeFrame(learned,local,0,0,probe,sizeof(probe),probeLength));
 assert(probeLength==80&&(dataLe16(probe)&255)==8&&!memcmp(probe+4,remote,6)&&!memcmp(probe+10,local,6));
 Peer syncPeer={};memcpy(syncPeer.address,remote,6);syncPeer.lastSeenGp2=50;
 syncPeer.hasSync=syncPeer.hasChannels=true;syncPeer.sync.awPeriod=16;syncPeer.sync.presenceMode=4;
 syncPeer.channels.count=16;syncPeer.channels.steps=4;syncPeer.channels.fill=0xffff;
 syncPeer.channels.entries[8].channel=6;
 NativeDiscoverySession learnedSession;
 assert(learnedSession.begin(2,1,2,local,1,20000000001ULL));learnedSession.open=true;
 assert(learnedSession.learnPeer(syncPeer,2,3,100));
 assert(!learnedSession.learnPeer(syncPeer,2,3,50+kPeerTimeoutUs+1));
 assert(learnedSession.peers[0].key.valid()&&learnedSession.peers[0].key.epoch==3);
 assert(learnedSession.learnPeer(syncPeer,2,4,100)&&learnedSession.peers[0].key.epoch==4);
 puts("PASS discovery lease/identity/generation, bounded copy/backpressure/budget, live RX/duplicate/sequence wrap/model reset/expiry, all 1048576 slot phases and stop admission");
 puts("PASS sync-advertisement peer learning and deterministic link-local probe frame");
}
