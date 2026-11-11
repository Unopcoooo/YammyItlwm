#include <cassert>
#include <cstring>
#include <cstdio>
#include <thread>
#include <atomic>
#include "TemplateActionBuilder.hpp"
namespace ItlAwdl {
TemplateRuntimeState gTemplateRuntime={};
TemplateRadioState gTemplateRadio={};
SyncTemplateState gSyncTemplate={};
}
using namespace ItlAwdl;
static uint16_t r16(const uint8_t*p){return uint16_t(p[0])|(uint16_t(p[1])<<8);}
static void fixture(uint8_t raw[110],const uint8_t mac[6]){
    memset(raw,0,110);unsigned n=0;
    raw[n++]=6;raw[n++]=9;raw[n++]=0;n+=9;
    raw[n++]=24;raw[n++]=40;raw[n++]=0;
    memcpy(raw+n,mac,6);memcpy(raw+n+6,mac,6);raw[n+20]=65;raw[n+24]=65;n+=40;
    raw[n++]=12;raw[n++]=47;raw[n++]=0;n+=47;
    raw[n++]=21;raw[n++]=2;raw[n++]=0;raw[n++]=16;raw[n++]=8;assert(n==110);
}
static bool contains(const uint8_t*p,size_t length,const char *text){
    size_t n=strlen(text);for(size_t i=0;i+n<=length;++i)if(!memcmp(p+i,text,n))return true;return false;
}
int main(){
    TemplateFrameInput i;
    i.sync.count=16;i.sync.encoding=1;i.sync.steps=4;
    i.sync.channels[0]=i.sync.channels[8]=0x062b;
    i.generation=7;i.revision=2;
    const uint8_t mac[6]={2,1,2,3,4,5};memcpy(i.service.source,mac,6);
    strcpy(i.service.instance,"fresh-instance");strcpy(i.service.host,"fresh-host");
    i.service.port=9123;i.service.txtLength=4;memcpy(i.service.txt,"\3x=y",4);
    assert(templateScheduleValid(i.sync,6));
    assert(i.clock.initialize(7,2,1000000,10000000000ULL,i.sync));
    uint32_t target;uint16_t targetAw;
    assert(i.clock.target(i.sync,1000000,target,targetAw));assert(target==1048576&&targetAw==64);
    i.targetGp2=target;i.startGp2=target+500;i.gp2=target+600;
    i.hostNs=i.clock.epochHostNs+uint64_t(uint32_t(i.gp2-i.clock.epochGp2))*1000;
    i.service.expiresNs=i.hostNs+5000000000ULL;i.leaseEndNs=i.hostNs+300000000000ULL;
    assert(templateFrameInputValid(i));
    uint8_t raw[110];fixture(raw,mac);uint8_t out[1536];size_t used=0;
    assert(buildTemplateAction(raw,110,i,9,out,sizeof(out),used)&&used==294);
    SyncParameters sync;
    assert(decodeSyncParameters(out+43,73,sync));
    assert(sync.nextAwSequence==64&&sync.txDownCounter==64&&sync.apAlignment==64);
    assert(sync.awPeriod==16&&sync.presenceMode==4&&sync.remainingAwLength==16);
    assert(!memcmp(out+10,mac,6));assert(out[120]==1&&r16(out+125)==0x062b);
    auto identityOnly=i;identityOnly.service.port=identityOnly.service.txtLength=0;
    identityOnly.service.host[0]=identityOnly.service.instance[0]=0;
    assert(buildTemplateAction(raw,110,identityOnly,10,out,sizeof(out),used));
    assert(!buildServiceMifAction(raw,110,identityOnly,10,out,sizeof(out),used));
    assert(buildServiceMifAction(raw,110,i,10,out,sizeof(out),used));
    assert(contains(out,used,"fresh-instance")&&contains(out,used,"fresh-host")&&contains(out,used,"x=y"));
    assert(!contains(out,used,"631e8a6b05fe")&&!contains(out,used,"deMacBook-Pro")&&!contains(out,used,"flags=13311"));
    // Compare the SRV port to the caller's current registration, independent of labels.
    bool portFound=false;for(size_t n=0;n+6<used;++n)if(out[n]==33&&out[n+1]==19&&out[n+2]==0){
        if(out[n+9]==(9123>>8)&&out[n+10]==(9123&255))portFound=true;
    }
    assert(portFound);
    auto changed=i;strcpy(changed.service.instance,"replacement");strcpy(changed.service.host,"renamed");
    changed.service.port=6543;changed.service.txt[2]='z';
    assert(buildServiceMifAction(raw,110,changed,11,out,sizeof(out),used));
    assert(contains(out,used,"replacement")&&contains(out,used,"renamed")&&!contains(out,used,"fresh-instance"));
    // The old fixed-zero fields must progress on a fixed epoch, not reset each frame.
    for(uint32_t age=0;age<300000000;age+=65536){
        uint32_t now=i.clock.epochGp2+age;
        assert(i.clock.aw(now,i.sync)==uint16_t(i.clock.epochAw+age/16384));
        assert(i.clock.down(now,i.sync)==64-(age/1024)%64);
    }
    assert(i.clock.target(i.sync,target+65536,target,targetAw));assert(targetAw==96);
    auto next=i;next.targetGp2=target;next.startGp2=target+500;next.gp2=target+1600;
    next.hostNs=i.clock.epochHostNs+uint64_t(uint32_t(next.gp2-i.clock.epochGp2))*1000;
    next.service.expiresNs=next.hostNs+4000000000ULL;
    assert(buildTemplateAction(raw,110,next,11,out,sizeof(out),used));
    assert(r16(out+72)==96&&r16(out+44)==63);
    auto wrapping=i;assert(wrapping.clock.initialize(7,2,0xfffffff0u,10000000000ULL,i.sync));
    assert(wrapping.clock.valid(7,2,0x100u,10000272000ULL));
    assert(!wrapping.clock.valid(8,2,0x100u,10000272000ULL));
    assert(!wrapping.clock.valid(7,3,0x100u,10000272000ULL));
    assert(!wrapping.clock.valid(7,2,0x100u,10010272000ULL));
    auto bad=i;bad.startGp2=bad.targetGp2+2001;bad.gp2=bad.startGp2;assert(!templateFrameInputValid(bad));
    bad=i;bad.startGp2=bad.targetGp2-1;assert(!templateFrameInputValid(bad));
    bad=i;bad.gp2=bad.targetGp2+64*1024-1999;assert(!templateFrameInputValid(bad));
    bad=i;bad.service.expiresNs=bad.hostNs;assert(!templateFrameInputValid(bad));
    bad=i;bad.service.txt[0]=255;assert(templateFrameInputValid(bad));assert(!templateServiceValid(bad.service,bad.hostNs));
    bad=i;memset(bad.service.host,'x',64);assert(templateFrameInputValid(bad));assert(!templateServiceValid(bad.service,bad.hostNs));
    bad=i;bad.service.source[0]|=1;assert(!templateFrameInputValid(bad));
    bad=i;bad.sync.channels[2]=0x952b;assert(!templateFrameInputValid(bad));
    bad=i;bad.sync.steps=3;assert(!templateFrameInputValid(bad));
    bad=i;bad.sync.presence=0;assert(!templateFrameInputValid(bad));
    // Master changes cannot change this transmitter's local address or invent a follower clock.
    raw[15]^=2;assert(!buildTemplateAction(raw,110,i,1,out,sizeof(out),used));fixture(raw,mac);
    assert(!buildTemplateAction(raw,109,i,1,out,sizeof(out),used));
    assert(!buildTemplateAction(raw,110,i,4096,out,sizeof(out),used));
    for(size_t capacity=0;capacity<294;++capacity)assert(!buildTemplateAction(raw,110,i,1,out,capacity,used));
    assert(publishSyncTemplate(raw,110));uint8_t snapshot[512];uint32_t size;
    assert(snapshotSyncTemplate(snapshot,sizeof(snapshot),size)&&size==110);
    assert(publishSyncTemplate(nullptr,0));assert(!snapshotSyncTemplate(snapshot,sizeof(snapshot),size));
    // Both readers and writers take the same try-lock; no torn template/config snapshots.
    std::atomic<bool> done(false);std::thread writer([&]{
        for(unsigned n=0;n<20000;++n){
            uint8_t data[110];memset(data,uint8_t(n),sizeof(data));publishSyncTemplate(data,sizeof(data));
            if(templateLock()){gTemplateRuntime.sync.period=uint16_t(n%64+1);gTemplateRuntime.sync.length=gTemplateRuntime.sync.period;templateUnlock();}
        }done=true;
    });
    while(!done){
        if(snapshotSyncTemplate(snapshot,sizeof(snapshot),size))for(unsigned n=1;n<size;++n)assert(snapshot[n]==snapshot[0]);
        TemplateSyncConfig c;TemplateService service;uint32_t revision;
        if(templateSnapshot(c,service,revision))assert(c.period==c.length);
    }
    writer.join();
    assert(templateLock());TemplateSyncConfig c;TemplateService service;uint32_t revision;
    assert(!templateSnapshot(c,service,revision));templateUnlock();
    puts("PASS current template clock/schedule/service: independent decoded AW progression, GP2 wrap and generation/clock drift, deadline/late/early guards, dynamic PTR/SRV/TXT and source identity, withdrawal and concurrent snapshots; no RF proof.");
}
