#include "../../itlwm/awdl/ActionBuilder.hpp"
#include "../../itlwm/awdl/ProtocolModel.hpp"
#include <assert.h>
#include <string.h>
#include <stdio.h>
using namespace ItlAwdl;
static ActionParameters fixture() {
    ActionParameters p = {}; const uint8_t source[6]={2,1,2,3,4,5};
    memcpy(p.source,source,6); memcpy(p.sync.masterAddress,source,6);
    memcpy(p.election.masterAddress,source,6); memcpy(p.election.syncAddress,source,6);
    p.sequence=0xabc; p.phyTransmitTime=0x12345678; p.targetTransmitTime=0x90abcdef;
    p.sync.awPeriod=16; p.sync.presenceMode=4; p.sync.txDownCounter=41;
    p.sync.afPeriod=110; p.sync.flags=0x1800; p.sync.awExtensionLength=16; p.sync.awCommonLength=16;
    p.sync.nextChannel=p.sync.masterChannel=6; p.sync.nextAwSequence=35405;
    p.channels.count=16; p.channels.steps=4; p.channels.encoding=3; p.channels.fill=65535;
    for(unsigned i=0;i<16;++i) p.channels.entries[i]={6,81};
    p.election.v2=true; p.election.masterMetric=60; p.election.selfMetric=60;
    p.election.masterCounter=0x12345678; p.election.selfCounter=0x90abcdef;
    return p;
}
int main() {
    auto p=fixture(); uint8_t storage[230]; size_t used;
    memset(storage,0xa5,sizeof(storage));
    for(size_t cap=0;cap<kControlActionLength;++cap) {
        assert(!buildControlAction(p,storage+1,cap,used) && used==0);
        for(auto b:storage) assert(b==0xa5);
    }
    assert(!buildControlAction(p,nullptr,kControlActionLength,used));
    uint8_t *out=storage+1; assert(buildControlAction(p,out,227,used) && used==227);
    assert(storage[0]==0xa5 && storage[228]==0xa5 && storage[229]==0xa5);
    // Independent expected header, not produced by the wire decoder.
    const uint8_t header[40]={0xd0,0,0,0,255,255,255,255,255,255,2,1,2,3,4,5,
        0,0x25,0,0xff,0x94,0x73,0xc0,0xab,127,0,0x17,0xf2,8,0x10,0,0,
        0x78,0x56,0x34,0x12,0xef,0xcd,0xab,0x90};
    assert(memcmp(out,header,40)==0);
    // Explicit TLV boundaries and padding, independently specified wire lengths.
    assert(out[40]==4 && out[41]==73 && out[42]==0);
    assert(out[76]==15 && out[77]==3 && out[79]==3 && out[114]==0 && out[115]==0);
    assert(out[116]==18 && out[117]==41 && out[118]==0);
    assert(out[157]==0 && out[158]==0 && out[159]==0);
    assert(out[160]==5 && out[161]==21 && out[182]==0 && out[183]==0);
    assert(out[184]==24 && out[185]==40);
    Advertisement a;
    assert(decodeAdvertisement(out,used,a)==AdvertisementResult::Valid);
    assert(a.sync.nextAwSequence==35405 && a.sync.txDownCounter==41 && a.sync.flags==0x1800);
    assert(a.channels.count==16 && a.channels.entries[15].auxiliary==81);
    assert(a.election.v2 && a.election.masterCounter==0x12345678 && a.election.selfCounter==0x90abcdef);
    // Refuse inconsistent advertised master and oversized counters before writes.
    auto bad=p; bad.sync.masterAddress[5]^=1;
    memset(storage,0xa5,sizeof(storage));
    assert(!buildControlAction(bad,out,227,used) && storage[1]==0xa5);
    bad=p; bad.sequence=4096; assert(!buildControlAction(bad,out,227,used));
    bad=p; bad.channels.steps=3; assert(!buildControlAction(bad,out,227,used));
    bad=p; bad.sync.txDownCounter=65; assert(!buildControlAction(bad,out,227,used));
    bad=p; bad.source[0]=1; assert(!buildControlAction(bad,out,227,used));
    p.subtype=3; assert(buildControlAction(p,out,227,used) && out[30]==3);
    puts("control action encoder: independent header/layout, capacity canaries and existing decoder cross-check PASS");
}
