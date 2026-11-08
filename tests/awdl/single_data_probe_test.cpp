// Pure packet/policy tests; no driver load or radio command.
#include "../../itlwm/awdl/SingleDataProbe.hpp"
#include <assert.h>
#include <stdio.h>
#include <initializer_list>
using namespace ItlAwdl;
static const uint8_t source[6]={2,1,2,3,4,5};
static SingleDataRequest request(){
    SingleDataRequest r;memcpy(r.peer,"\x02\x06\x07\x08\x09\x0a",6);
    r.sourceIp[0]=r.peerIp[0]=0xfe;r.sourceIp[1]=r.peerIp[1]=0x80;
    r.sourceIp[15]=1;r.peerIp[15]=2;r.token=0x1122334455667788ULL;return r;
}
static unsigned checksum(const uint8_t *ip){
    unsigned sum=0;for(unsigned i=8;i<64;i+=2)sum+=(unsigned(ip[i])<<8)|ip[i+1];
    sum+=24;sum+=58;while(sum>65535)sum=(sum&65535)+(sum>>16);return sum;
}
struct Environment {
    PeerTable peers;AuxRocSession roc;DataContext c;
    Environment(){
        auto r=request();peers.count=1;auto &p=peers.peers[0];memcpy(p.address,r.peer,6);
        p.hasSync=p.hasChannels=true;p.channels.count=16;p.channels.steps=4;p.channels.fill=65535;p.channels.encoding=3;
        for(auto &ch:p.channels.entries){ch.channel=6;ch.auxiliary=0x51;}
        for(unsigned down:{40u,24u,8u}){
            p.sync={};p.sync.awPeriod=16;p.sync.presenceMode=4;p.sync.txDownCounter=down;p.sync.nextAwSequence=65532;
            p.syncReceivedGp2=1000000-down*1024;p.lastSeenGp2=p.syncReceivedGp2;
            p.timing.observe(p.sync,p.channels,p.syncReceivedGp2);
        }
        assert(p.timing.streak==3&&p.timing.consistent);
        roc.resetAfterDeviceQuiesced(1);assert(roc.begin(1));uint8_t response[8]={};rocWrite32(response,77);
        assert(roc.response(response,8,1)&&roc.apply({1000500,77,4,1,1}));
        c.peers=&peers;c.roc=&roc;c.generation=c.modelGeneration=c.modelEpoch=1;c.gp2=1003000;
        c.channel=6;c.durationUs=58*1024;c.targetGp2=1000000;c.nowNs=10000000000ULL;
        c.modelHostNs=c.nowNs-1000000;c.deadlineNs=c.nowNs+1000000000;c.gateHeld=c.associated=c.aligned=true;
    }
};
int main(){
    auto r=request();uint8_t wire[104];size_t n=0;
    assert(buildSingleDataEcho(r,source,1,23,wire,sizeof(wire),n)&&n==104);
    assert(wire[0]==8&&dataLe16(wire+22)==23*16&&wire[80]==128&&wire[84]==0x77&&wire[85]==0x88);
    assert(!memcmp(wire+88,"ADP1\x11\x22\x33\x44\x55\x66\x77\x88\0\0\0\1",16));
    assert(checksum(wire+40)==65535&&probeChecksum(wire+40)==0);
    uint8_t ethernet[78];size_t size;uint16_t seq;
    assert(decodeData(wire,104,r.peer,source,ethernet,78,size,seq)==DataError::Ok&&size==78&&seq==23);
    assert(!buildSingleDataEcho(r,source,1,0,wire,103,n)&&!n);
    for(unsigned bad=0;bad<5;++bad){auto x=r;
        if(bad==0)x.token=0;if(bad==1)x.peer[0]=3;if(bad==2)memcpy(x.peer,source,6);
        if(bad==3)x.sourceIp[3]=1;if(bad==4)memcpy(x.peerIp,x.sourceIp,16);
        assert(!buildSingleDataEcho(x,source,1,0,wire,104,n));
    }
    Environment e;SingleDataProbe p;
    assert(!p.arm(r,source,1,e.c.nowNs,e.c.nowNs+499999999));
    assert(p.arm(r,source,1,e.c.nowNs,e.c.nowNs+60000000000ULL));
    assert(p.deadlineNs==e.c.nowNs+30000000000ULL&&!p.arm(r,source,1,e.c.nowNs,e.c.deadlineNs));
    assert(!e.c.backendReady&&p.eligible(e.c)&&!dataActualWindow(e.c,true));
    auto invalid=e.c;invalid.gateHeld=false;assert(!p.eligible(invalid));
    invalid=e.c;invalid.modelHostNs-=500000000;assert(!p.eligible(invalid)&&p.lastBlockReason==31);
    invalid=e.c;invalid.gp2=1001000;assert(!p.eligible(invalid));
    invalid=e.c;invalid.gp2=e.roc.startGp2+e.c.durationUs-4999;assert(!p.eligible(invalid));
    e.peers.peers[0].timing.streak=2;assert(!p.eligible(e.c)&&p.lastBlockReason==34);e.peers.peers[0].timing.streak=3;
    e.peers.peers[0].channels.entries[0].channel=44;assert(!p.eligible(e.c));e.peers.peers[0].channels.entries[0].channel=6;
    e.peers.peers[0].sync.awPeriod=65535;assert(!p.eligible(e.c));e.peers.peers[0].sync.awPeriod=16;
    e.roc.phase=RocPhase::AwaitStart;assert(!p.eligible(e.c)&&p.lastBlockReason==32);e.roc.phase=RocPhase::Running;
    assert(p.begin(1,1,e.c.nowNs)&&p.eligible(e.c));invalid=e.c;invalid.modelEpoch=2;assert(!p.eligible(invalid));
    assert(buildSingleDataEcho(r,source,1,23,wire,sizeof(wire),n));
    assert(p.publish(1,3,9,e.c.gp2,e.c.nowNs,wire,n)&&p.pending&&p.submitted==1);
    p.cancel();p.tick(2,e.c.nowNs+60000000000ULL);assert(p.pending); // Cancellation is not DMA quiescence.
    assert(!p.complete(2,3,9,1,true)&&!p.complete(1,3,8,1,true));
    assert(p.complete(1,3,9,2,false)&&!p.pending&&!p.ackReported&&p.completed==1);
    assert(!p.complete(1,3,9,1,true));
    // Build the peer's matching echo reply independently from the request Ethernet packet.
    memcpy(ethernet,source,6);memcpy(ethernet+6,r.peer,6);memcpy(ethernet+22,r.peerIp,16);memcpy(ethernet+38,r.sourceIp,16);
    auto ip=ethernet+14;ip[40]=129;ip[42]=ip[43]=0;const uint16_t sum=uint16_t(~checksum(ip));ip[42]=sum>>8;ip[43]=sum;
    assert(checksum(ip)==65535&&encodeData(ethernet,78,r.peer,24,24,wire,104,n)==DataError::Ok);
    assert(!p.reply(wire,104,2,6,e.c.gp2+100,e.c.nowNs+100000));
    assert(!p.reply(wire,104,1,44,e.c.gp2+100,e.c.nowNs+100000));
    wire[100]^=1;assert(!p.reply(wire,104,1,6,e.c.gp2+100,e.c.nowNs+100000));wire[100]^=1;
    assert(!p.reply(wire,104,1,6,e.c.gp2+5000001,e.c.nowNs+5000000001ULL));
    assert(p.reply(wire,104,1,6,e.c.gp2+100,e.c.nowNs+100000)&&!p.reply(wire,104,1,6,e.c.gp2+100,e.c.nowNs+100000));
    p.replyObserved=false;p.quiesced();assert(!p.reply(wire,104,1,6,e.c.gp2+100,e.c.nowNs+100000));
    assert(p.everArmed&&!p.arm(r,source,1,e.c.nowNs,e.c.deadlineNs));
    SingleDataProbe expired;assert(expired.arm(r,source,1,e.c.nowNs,e.c.deadlineNs));expired.blocked(34);expired.tick(1,e.c.deadlineNs);
    assert(expired.phase==SingleDataPhase::Expired&&expired.reason==1&&expired.lastBlockReason==34&&!expired.begin(1,1,e.c.nowNs));
    SingleDataProbe reset;assert(reset.arm(r,source,1,e.c.nowNs,e.c.deadlineNs));reset.tick(2,e.c.nowNs);
    assert(reset.phase==SingleDataPhase::Aborted&&!reset.eligible(e.c));
    puts("PASS single data probe: independent ICMP checksum/wire decode, strict request/window/peer/epoch guards, once-per-boot budget, exact completion, cancel retains DMA, exact peer reply and replay/timeout rejection. No RF.");
}
