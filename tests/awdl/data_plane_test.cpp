// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../itlwm/awdl/DataQueue.hpp"
#include <assert.h>
#include <stdio.h>
#include <vector>
#include <algorithm>
using namespace ItlAwdl;
static const uint8_t local[6]={2,1,2,3,4,5},peer[6]={2,6,7,8,9,10};
static std::vector<uint8_t> ethernet(size_t ipSize,bool multicast=false){
    std::vector<uint8_t> p(14+ipSize,0);
    memcpy(p.data(),peer,6);memcpy(p.data()+6,local,6);p[12]=0x86;p[13]=0xdd;
    auto ip=p.data()+14;ip[0]=0x60;ip[4]=(ipSize-40)>>8;ip[5]=(ipSize-40)&255;
    ip[6]=17;ip[7]=255;ip[8]=0xfe;ip[9]=0x80;ip[23]=1;ip[24]=0xfe;ip[25]=0x80;ip[39]=2;
    if(multicast){ip[24]=0xff;ip[25]=2;ip[39]=0xfb;memset(ip+26,0,13);
        const uint8_t mac[6]={0x33,0x33,0,0,0,0xfb};memcpy(p.data(),mac,6);}
    for(size_t i=40;i<ipSize;++i)ip[i]=uint8_t(i);
    return p;
}
static std::vector<uint8_t> wire(const std::vector<uint8_t> &eth){
    std::vector<uint8_t> out(kAwdlDataMax);size_t n=999;
    assert(encodeData(eth.data(),eth.size(),local,0xabc,0x1234,out.data(),out.size(),n)==DataError::Ok);
    out.resize(n);return out;
}
static void codecs(){
    auto eth=ethernet(40);auto w=wire(eth);
    // Independently specified complete header: IEEE 802.11, Apple LLC, AWDL data.
    const uint8_t golden[]={8,0,0,0,2,6,7,8,9,10,2,1,2,3,4,5,
        0,0x25,0,0xff,0x94,0x73,0xc0,0xab,0xaa,0xaa,3,0,0x17,0xf2,8,0,
        3,4,0x34,0x12,0,0,0x86,0xdd};
    assert(w.size()==80&&!memcmp(w.data(),golden,sizeof(golden)));
    uint8_t out[kEtherDataMax+16];size_t used;uint16_t seq;
    for(size_t size=40;size<=1500;++size)for(bool multi:{false,true}){
        eth=ethernet(size,multi);w=wire(eth);
        memset(out,0xa5,sizeof(out));
        assert(decodeData(w.data(),w.size(),peer,local,out,sizeof(out),used,seq)==DataError::Ok);
        assert(used==eth.size()&&seq==0x1234&&!memcmp(out,eth.data(),used));
        assert(std::all_of(out+used,out+sizeof(out),[](uint8_t b){return b==0xa5;}));
        w[0]=0x88;w.insert(w.begin()+24,{3,0});
        assert(decodeData(w.data(),w.size(),peer,local,out,sizeof(out),used,seq)==DataError::Ok);
        assert(used==eth.size()&&!memcmp(out,eth.data(),used));
        w[24]=0x80;assert(decodeData(w.data(),w.size(),peer,local,out,sizeof(out),used,seq)!=DataError::Ok);
    }
    w=wire(ethernet(80));auto bad=w;
    for(unsigned offset:{1u,16u,24u,27u,28u,29u,30u,32u,33u,36u,37u,38u,39u,40u,44u,45u}){
        bad=w;bad[offset]^=0xff;memset(out,0xa5,sizeof(out));
        assert(decodeData(bad.data(),bad.size(),peer,local,out,sizeof(out),used,seq)!=DataError::Ok);
        assert(!used&&std::all_of(out,out+sizeof(out),[](uint8_t b){return b==0xa5;}));
    }
    for(uint8_t flags:{uint8_t(1),uint8_t(2),uint8_t(4),uint8_t(0x40),uint8_t(0x80)}){
        bad=w;bad[1]=flags;assert(decodeData(bad.data(),bad.size(),peer,local,out,sizeof(out),used,seq)!=DataError::Ok);
    }
    bad=w;bad[22]|=1;assert(decodeData(bad.data(),bad.size(),peer,local,out,sizeof(out),used,seq)!=DataError::Ok);
    assert(decodeData(w.data(),w.size(),local,local,out,sizeof(out),used,seq)==DataError::Peer);
    assert(decodeData(w.data(),w.size(),peer,peer,out,sizeof(out),used,seq)==DataError::Peer);
    assert(decodeData(w.data(),w.size(),peer,local,out,53,used,seq)==DataError::Capacity);
    assert(decodeData(w.data(),w.size(),peer,local,w.data(),w.size(),used,seq)==DataError::Alias);
    memcpy(out,local,6);assert(decodeData(w.data(),w.size(),peer,out,out,sizeof(out),used,seq)==DataError::Alias);
    eth=ethernet(40);uint8_t encoded[kAwdlDataMax];
    memcpy(encoded,local,6);
    assert(encodeData(eth.data(),eth.size(),encoded,0,0,encoded,sizeof(encoded),used)==DataError::Alias);
    assert(encodeData(eth.data(),eth.size(),local,4096,0,encoded,sizeof(encoded),used)==DataError::Layout);
    assert(encodeData(eth.data(),eth.size(),local,0,0,encoded,79,used)==DataError::Capacity);
    assert(encodeData(eth.data(),eth.size(),local,0,0,eth.data(),eth.size()+26,used)==DataError::Alias);
    auto multi=ethernet(40,true);multi[5]^=1;
    assert(encodeData(multi.data(),multi.size(),local,0,0,encoded,sizeof(encoded),used)==DataError::Address);
    for(size_t n=0;n<w.size();++n){
        std::vector<uint8_t> exact(w.begin(),w.begin()+n);
        assert(decodeData(exact.data(),n,peer,local,out,sizeof(out),used,seq)!=DataError::Ok);
    }
    // Deterministic mutations with exact-length allocations expose overreads to ASan.
    uint32_t seed=1;
    for(unsigned i=0;i<30000;++i){
        seed=seed*1664525u+1013904223u;size_t n=seed%1600;
        std::vector<uint8_t> fuzz(n);for(auto &b:fuzz){seed=seed*1664525u+1013904223u;b=seed>>24;}
        if(n>=w.size()&&(i&1)){memcpy(fuzz.data(),w.data(),w.size());fuzz[seed%w.size()]^=uint8_t(seed>>16);}
        auto result=decodeData(fuzz.data(),fuzz.size(),peer,local,out,sizeof(out),used,seq);
        assert(result!=DataError::Ok||(used>=54&&used<=kEtherDataMax));
        encodeData(fuzz.data(),fuzz.size(),local,seed&4095,seed,encoded,sizeof(encoded),used);
    }
    puts("PASS frame golden, 2922 plain + QoS round trips, rejection/alias/bounds, 30000 mutations");
}
struct Packet {std::vector<uint8_t> data=ethernet(80);unsigned buffers=1,refs=1,completed=0;};
struct Queue {unsigned refs=1;};
struct Ops {
    using Packet=::Packet;using Queue=::Queue;
    static unsigned bufferCount(Packet *p){return p->buffers;}
    static size_t length(Packet *p){return p->data.size();}
    static const uint8_t *bytes(Packet *p){return p->data.data();}
    static void retain(Packet *p,Queue *q){++p->refs;++q->refs;}
    static void release(Packet *p,Queue *q){assert(p->refs>1&&q->refs>1);--p->refs;--q->refs;}
    static int complete(Packet *p,Queue *,int status){assert(status==0||status==-1);++p->completed;return 0;}
};
static void lifecycle(){
    DataQueue q;Queue native;Packet packets[6];DataTransfer tx[4];DataCompletion done;
    assert(!offerDataPacket<Ops>(q,&packets[0],&native,1,1,0,0));
    assert(!q.open(0,local,1,10));assert(!q.open(1,local,1,300000000002ULL));
    assert(q.open(1,local,1,100));assert(!q.open(1,local,1,200));
    packets[5].buffers=2;assert(!offerDataPacket<Ops>(q,&packets[5],&native,1,2,0,0));
    for(unsigned i=0;i<4;++i)assert(offerDataPacket<Ops>(q,&packets[i],&native,1,2,i,i));
    assert(!offerDataPacket<Ops>(q,&packets[4],&native,1,2,4,4));assert(packets[4].refs==1);
    assert(!q.take(2,3,tx[0]));
    for(unsigned i=0;i<4;++i){assert(q.take(1,3,tx[i]));assert(tx[i].ticket.serial==i+1);}
    assert(!q.finish({1,2,0},done));assert(!q.reclaim(true,done));
    assert(q.finish(tx[2].ticket,done));assert(done.packet==&packets[2]);
    completeDataPacket<Ops>(done,0);completeDataPacket<Ops>(done,0);assert(packets[2].completed==1);
    assert(!q.finish(tx[2].ticket,done));
    assert(offerDataPacket<Ops>(q,&packets[4],&native,1,3,4,4));
    assert(!q.finish(tx[2].ticket,done));assert(!q.take(1,100,tx[2]));
    q.stop();assert(q.reclaim(false,done));assert(done.packet==&packets[4]);completeDataPacket<Ops>(done,-1);
    assert(!q.reclaim(false,done));assert(!q.open(2,local,100,200));
    unsigned reclaimed=0;while(q.reclaim(true,done)){completeDataPacket<Ops>(done,-1);++reclaimed;}
    assert(reclaimed==3&&q.empty()&&native.refs==1);
    for(unsigned i=0;i<5;++i)assert(packets[i].refs==1&&packets[i].completed==1);
    assert(q.open(2,local,100,200));assert(!q.finish(tx[0].ticket,done));
    assert(!offerDataPacket<Ops>(q,&packets[5],&native,1,101,0,0));
    packets[5].buffers=1;assert(offerDataPacket<Ops>(q,&packets[5],&native,2,101,0,0));
    assert(q.take(2,102,tx[0]));assert(tx[0].ticket.serial==6);
    assert(q.finish(tx[0].ticket,done));completeDataPacket<Ops>(done,0);
    q.stop();q.serial=UINT64_MAX;assert(q.open(3,local,200,300));
    assert(!offerDataPacket<Ops>(q,&packets[5],&native,3,201,0,0));assert(packets[5].refs==1&&native.refs==1);
    puts("PASS bounded queue, FIFO, ownership/backpressure, stale/duplicate completion, expiry, reset and DMA quarantine");
}
int main(){codecs();lifecycle();}
