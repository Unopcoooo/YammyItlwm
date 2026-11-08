// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../itlwm/awdl/DataPort.hpp"
#include <assert.h>
#include <stdio.h>
#include <vector>
#include "../../itlwm/awdl/LocalDataIdentity.hpp"
using namespace ItlAwdl;
struct Object {
    void *workloop=this;bool queueEnabled=true;
    const char *kind;Object *pool=nullptr,*source=nullptr;
    unsigned refs=1,buffers=1,capacity=2048,length=0,credit=0,recycled=0;
    int completionStatus=0;int64_t offset=0;
    bool prepared=true,handle=true,missingBytes=false;
    std::vector<uint8_t> data=std::vector<uint8_t>(2080,0xa5);
    explicit Object(const char *k):kind(k){}
    void *metaCast(const char *k){return !strcmp(kind,k)||(!strcmp(kind,"IOSkywalkNetworkPacket")&&!strcmp(k,"IOSkywalkPacket"))?this:nullptr;}
    void retain(){assert(refs);++refs;}void release(){assert(refs>1);--refs;}
};
static Object rx("IOSkywalkNetworkPacket");
static DataPort<Object> *linkTarget=nullptr;static unsigned upCalls=0,downCalls=0,linkFault=0;
static bool testLinkAction(Object*,bool up){
 if(up){++upCalls;if(linkFault==2){linkTarget->stop(false,ECANCELED);assert(!linkTarget->unbind());}return linkFault!=1;}
 ++downCalls;return linkFault!=3;
}
static unsigned txNotifications=0;
static unsigned notifications=0;static int notificationResult=0;
static unsigned allocations=0,failStage=0;static int statusResult=0;static bool badMulti=false;
static DataCompletion *reentrant=nullptr;
static DataPort<Object> *stopDuringRecycle=nullptr;
static std::vector<uint8_t> eth(){
    std::vector<uint8_t> e(94);const uint8_t src[6]={2,1,2,3,4,5},dst[6]={2,6,7,8,9,10};
    memcpy(e.data(),dst,6);memcpy(e.data()+6,src,6);e[12]=0x86;e[13]=0xdd;
    e[14]=0x60;e[19]=40;e[20]=17;e[21]=255;e[22]=0xfe;e[38]=0xfe;return e;
}
namespace ItlAwdl {
extern "C" void *dataNativeWorkLoop(const void *q){return static_cast<const Object *>(q)->workloop;}
extern "C" bool dataNativeQueueEnabled(const void *q){return static_cast<const Object *>(q)->queueEnabled;}
extern "C" int dataNativeRequestTransmit(void *q,void *context,uint32_t options){
 assert(q&&static_cast<Object *>(q)->metaCast("IOSkywalkTxSubmissionQueue")&&!context&&options==1);
 ++txNotifications;return 0;
}
extern "C" int dataNativeRequestReceive(void *q,void *context,uint32_t options){
 assert(q&&static_cast<Object *>(q)->metaCast("IOSkywalkRxCompletionQueue")&&!context&&options==1);
 ++notifications;return notificationResult;
}
extern "C" uint32_t dataNativeBufferCount(const void *v){return static_cast<const Object *>(v)->buffers;}
extern "C" uint32_t dataNativeLength(const void *v){return static_cast<const Object *>(v)->length;}
extern "C" uint32_t dataNativeBufferSize(const void *v){return static_cast<const Object *>(v)->capacity;}
extern "C" int64_t dataNativeOffset(const void *v){return static_cast<const Object *>(v)->offset;}
extern "C" uint8_t *dataNativeBufferBase(const void *v){auto p=const_cast<Object *>(static_cast<const Object *>(v));return p->missingBytes?nullptr:p->data.data();}
extern "C" void *dataNativePacketPool(const void *v){return static_cast<const Object *>(v)->pool;}
extern "C" void *dataNativeQueuePool(const void *v){return static_cast<const Object *>(v)->pool;}
extern "C" void *dataNativeSource(const void *v){return static_cast<const Object *>(v)->source;}
extern "C" int dataNativeStatus(void *v,int status){auto p=static_cast<Object *>(v);assert(p->handle);p->completionStatus=status;return statusResult;}
extern "C" void dataNativeRecycle(void *owner,void *v){
    auto p=static_cast<Object *>(v);assert(p->pool==owner&&p->handle);
    if(stopDuringRecycle){auto port=stopDuringRecycle;stopDuringRecycle=nullptr;port->stop(false,ECANCELED);assert(port->unbind());}
    if(reentrant){auto token=reentrant;reentrant=nullptr;completeDataPacket<NativeDataOps<Object>>(*token,0);}
    if(p->prepared&&p->source)++p->source->credit;
    p->prepared=false;p->source=nullptr;p->handle=false;++p->recycled;
}
extern "C" int dataNativeAllocate(void *owner,uint32_t count,void **out,uint32_t options){
    assert(count==1&&options==1);++allocations;if(failStage==1)return -1;
    assert(!rx.handle);rx.pool=static_cast<Object *>(owner);rx.source=nullptr;rx.handle=true;
    rx.prepared=false;rx.buffers=0;rx.capacity=failStage==4?10:2048;rx.offset=7;rx.length=0;
    rx.missingBytes=failStage==6;*out=&rx;return 0;
}
extern "C" int dataNativePrepare(void *v,void *queue,uint32_t direction,uint32_t options){
    auto p=static_cast<Object *>(v);assert(!queue&&!direction&&!options);
    if(failStage==2)return -1;p->prepared=true;p->buffers=badMulti?2:1;return 0;
}
extern "C" int dataNativeSetRange(void *v,uint16_t offset,uint32_t length){
    if(failStage==5)return -1;auto p=static_cast<Object *>(v);
    assert(offset+length<=p->capacity);p->offset=offset;p->length=length;return 0;
}
}
static void portTests(){
    Object owner("owner"),pool("IOSkywalkPacketBufferPool"),txq("IOSkywalkTxSubmissionQueue"),rxq("IOSkywalkRxCompletionQueue");
    txq.pool=rxq.pool=&pool;auto e=eth();const auto local=e.data()+6;
    PeerTable peers={};peers.count=1;auto &peer=peers.peers[0];memcpy(peer.address,e.data(),6);
    peer.hasSync=peer.hasChannels=peer.hasElection=true;memcpy(peer.election.masterAddress,local,6);
    peer.channels.count=16;peer.channels.steps=4;peer.channels.fill=0xffff;peer.channels.encoding=3;
    for(auto &channel:peer.channels.entries){channel.channel=6;channel.auxiliary=0x51;}
    const uint32_t target=1000000;
    for(unsigned down:{40u,24u,8u}){
        peer.sync={};peer.sync.awPeriod=16;peer.sync.presenceMode=4;peer.sync.txDownCounter=down;peer.sync.nextAwSequence=65532;
        memcpy(peer.sync.masterAddress,local,6);peer.syncReceivedGp2=target-down*1024;
        peer.lastSeenGp2=peer.syncReceivedGp2;peer.timing.observe(peer.sync,peer.channels,peer.syncReceivedGp2);
    }
    assert(peer.timing.consistent&&peer.timing.streak==3);
    AuxRocSession roc;roc.resetAfterDeviceQuiesced(1);assert(roc.begin(1));
    uint8_t response[8]={};rocWrite32(response,77);assert(roc.response(response,sizeof(response),1));
    assert(roc.apply({target+500,77,4,1,1}));assert(roc.phase==RocPhase::Running);
    DataContext c;c.peers=&peers;c.roc=&roc;c.generation=c.modelGeneration=c.modelEpoch=1;
    c.gp2=target+3000;c.targetGp2=target;c.aw=0;c.channel=6;c.durationUs=58*1024;
    c.nowNs=10000000000ULL;c.modelHostNs=c.nowNs-1000000;c.deadlineNs=c.nowNs+1000000000;
    c.gateHeld=c.backendReady=c.associated=c.aligned=true;
    assert(dataDestinationAllowed(e.data(),local,c));
    auto invalid=c;invalid.backendReady=false;assert(!dataDestinationAllowed(e.data(),local,invalid));
    invalid=c;invalid.gateHeld=false;assert(!dataDestinationAllowed(e.data(),local,invalid));
    invalid=c;invalid.modelGeneration=2;assert(!dataDestinationAllowed(e.data(),local,invalid));
    invalid=c;invalid.modelHostNs=c.nowNs-500000001;assert(!dataDestinationAllowed(e.data(),local,invalid));
    invalid=c;invalid.gp2=roc.startGp2+c.durationUs-4999;assert(!dataDestinationAllowed(e.data(),local,invalid));
    invalid=c;invalid.aw=4;assert(!dataDestinationAllowed(e.data(),local,invalid));
    roc.phase=RocPhase::AwaitStart;assert(!dataDestinationAllowed(e.data(),local,c));roc.phase=RocPhase::Running;
    peer.sync.masterAddress[0]^=4;assert(!dataDestinationAllowed(e.data(),local,c));peer.sync.masterAddress[0]^=4;
    peer.channels.entries[0].channel=44;assert(!dataDestinationAllowed(e.data(),local,c));peer.channels.entries[0].channel=6;
    const uint8_t multicast[6]={0x33,0x33,0,0,0,0xfb};assert(dataDestinationAllowed(multicast,local,c));
    // Exclusive IBSS is independent of ordinary STA association and AUX START.
    {
     Object txq("IOSkywalkTxSubmissionQueue"),rxq("IOSkywalkRxCompletionQueue");txq.pool=rxq.pool=&pool;
     auto x=c;x.radioKind=DataRadioKind::ExclusiveIbss;x.associated=false;x.roc=nullptr;x.aligned=false;
     DataIbssLease lease;lease.generation=1;lease.deadlineNs=x.deadlineNs;
     memcpy(lease.local,local,6);memcpy(lease.peer,peer.address,6);
     lease.ownsRadio=lease.firmwareReady=lease.peerReady=lease.peerFollowValid=true;
     ItlIbss::WindowProbe w;w.phase=ItlIbss::WindowProbe::Open;w.plan.generation=1;w.plan.epoch=x.modelEpoch;
     w.plan.version=2;w.plan.period=16384;w.plan.boundary=x.gp2-3048;
     w.plan.open=w.plan.boundary+2048;w.plan.close=w.plan.boundary+w.plan.period-2048;w.plan.observedGp2=x.gp2;
     w.plan.observedHost=x.nowNs;w.plan.peer=peer.diagnosticId=1;lease.window=&w;x.ibss=&lease;
     // Failed startup never publishes admission or drops the owner's lifetime pin.
     for(unsigned fault=0;fault<8;++fault){
      DataPort<Object> pending;const auto held=owner.refs;
      assert(pending.bind(&owner,&pool,&txq,&rxq,local)&&owner.refs==held+1);
      auto prepared=x;auto copied=lease;prepared.ibss=&copied;prepared.backendReady=false;
      uint8_t current[6];memcpy(current,local,6);auto end=x.deadlineNs;
      if(fault==0)current[5]^=2;if(fault==1)prepared.gateHeld=false;
      if(fault==2)copied.ownsRadio=false;if(fault==3)prepared.generation++;
      if(fault==4)end++;if(fault==5)rxq.queueEnabled=false;
      if(fault==6)copied.local[5]^=2;if(fault==7)end=x.nowNs+499999999;
      assert(!pending.openNative(prepared,current,end)&&!pending.tx.enabled&&!pending.nativeSession&&pending.tx.empty());
      rxq.queueEnabled=true;assert(pending.unbind()&&owner.refs==held);
     }
     for(unsigned fault=0;fault<4;++fault){
      DataPort<Object> linked;assert(linked.bind(&owner,&pool,&txq,&rxq,local));
      auto prepared=x;prepared.backendReady=false;assert(linked.openNative(prepared,local,x.deadlineNs));
      linked.linkAction=testLinkAction;linkTarget=&linked;linkFault=fault;upCalls=downCalls=0;
      assert(linked.publishNativeLink(true)==(fault==0||fault==3));
      if(fault==1||fault==2)assert(!linked.linkUp&&!linked.linkDownPending&&downCalls==1);
      linked.stop(false,ECANCELED);
      if(fault==3){assert(linked.linkDownPending&&!linked.unbind());linkFault=0;assert(linked.publishNativeLink(false));}
      assert(!linked.linkUp&&!linked.linkDownPending&&linked.unbind()&&upCalls==1);
     }
     linkTarget=nullptr;linkFault=0;
     const auto savedMaster=peer.sync.masterAddress[0];peer.sync.masterAddress[0]^=4;
     assert(dataDestinationAllowed(e.data(),local,x)&&!dataDestinationAllowed(multicast,local,x));
     DataPort<Object> native;assert(native.bind(&owner,&pool,&txq,&rxq,local));
     auto prepared=x;prepared.backendReady=false;
     assert(native.openNative(prepared,local,x.deadlineNs)&&native.nativeSession&&native.tx.enabled);
     assert(!native.openNative(prepared,local,x.deadlineNs)&&native.nativeSession&&native.tx.enabled);
     Object unsupported("IOSkywalkNetworkPacket"),unknown("IOSkywalkNetworkPacket"),unicast("IOSkywalkNetworkPacket");
     for(auto p:{&unsupported,&unknown,&unicast}){p->pool=&pool;p->source=&txq;p->length=e.size();memcpy(p->data.data(),e.data(),e.size());}
     const uint8_t multicastDestination[6]={0x33,0x33,0,0,0,1};memcpy(unsupported.data.data(),multicastDestination,6);
     memset(unsupported.data.data()+38,0,16);unsupported.data[38]=0xff;unsupported.data[39]=2;unsupported.data[53]=1;
     unknown.data[5]^=2;
     Object *offered[]={&unsupported,&unknown,&unicast};
     const auto signals=txNotifications;assert(native.notifyTransmit()==0&&txNotifications==signals+1);
     assert(native.offer(&owner,&txq,offered,3,x)==3);
     assert(!unsupported.handle&&unsupported.completionStatus==EOPNOTSUPP);
     assert(!unknown.handle&&unknown.completionStatus==EHOSTUNREACH&&unicast.handle);
     auto incoming=e;memcpy(incoming.data(),local,6);memcpy(incoming.data()+6,peer.address,6);
     uint8_t wire[kAwdlDataMax];size_t n;
     assert(encodeData(incoming.data(),incoming.size(),peer.address,1,99,wire,sizeof(wire),n)==DataError::Ok);
     w.phase=ItlIbss::WindowProbe::Closed;
     assert(!dataDestinationAllowed(e.data(),local,x)&&native.receive(wire,n,x));
     const auto signaled=notifications;notificationResult=-7;
     assert(native.notifyReceive()==-7&&native.rxCount==1&&notifications==signaled+1);
     notificationResult=0;assert(native.notifyReceive()==0&&native.rxCount==1&&notifications==signaled+2);
     Object *packet=nullptr;assert(native.poll(&owner,&rxq,&packet,1,x)==1&&packet);
     assert(!memcmp(packet->data.data(),incoming.data(),incoming.size())&&recycleNativeDataReceive(packet));
     native.stop(false,ECANCELED);assert(native.nativeStopPending&&native.notifyReceive()==-1&&native.unbind()&&!native.nativeStopPending);
     w.phase=ItlIbss::WindowProbe::Open;
     for(unsigned fault=0;fault<10;++fault){auto bad=x;auto badLease=lease;auto badWindow=w;bad.ibss=&badLease;badLease.window=&badWindow;
      if(fault==0)badLease.ownsRadio=false;if(fault==1)badLease.firmwareReady=false;
      if(fault==2)badLease.peerReady=false;if(fault==3)badLease.resetRequested=true;
      if(fault==4)badLease.generation++;if(fault==5)badLease.deadlineNs=bad.nowNs;
      if(fault==6)badLease.peerFollowValid=false;if(fault==7)badLease.local[5]^=2;
      if(fault==8)badWindow.plan.peer++;if(fault==9)badWindow.plan.epoch++;
      assert(!dataDestinationAllowed(e.data(),local,bad));
     }
     auto bad=x;bad.backendReady=false;assert(!dataDestinationAllowed(e.data(),local,bad));
     peer.sync.masterAddress[0]=savedMaster;
    }
    const uint32_t shift=uint32_t(0xfffff000u-target);
    c.gp2+=shift;c.targetGp2+=shift;roc.startGp2+=shift;peer.lastSeenGp2+=shift;
    peer.syncReceivedGp2+=shift;peer.timing.receivedGp2+=shift;
    assert(dataDestinationAllowed(e.data(),local,c));
    c.gp2-=shift;c.targetGp2-=shift;roc.startGp2-=shift;peer.lastSeenGp2-=shift;
    peer.syncReceivedGp2-=shift;peer.timing.receivedGp2-=shift;
    DataPort<Object> port;assert(port.bind(&owner,&pool,&txq,&rxq));assert(port.open(1,local,c.nowNs-1000,c.deadlineNs));
    Object a("IOSkywalkNetworkPacket"),b("IOSkywalkPacket"),d("IOSkywalkNetworkPacket");
    for(auto p:{&a,&b,&d}){p->pool=&pool;p->source=&txq;p->length=e.size();memcpy(p->data.data(),e.data(),e.size());}
    Object malformed("IOSkywalkNetworkPacket");malformed.pool=&pool;malformed.source=&txq;
    malformed.length=e.size();memcpy(malformed.data.data(),e.data(),e.size());malformed.data[12]=8;
    Object otherBad("IOSkywalkNetworkPacket");otherBad.pool=&pool;otherBad.source=&txq;
    otherBad.length=e.size();memcpy(otherBad.data.data(),malformed.data.data(),e.size());
    Object *badPrefix[]={&malformed,&otherBad};stopDuringRecycle=&port;
    assert(port.offer(&owner,&txq,badPrefix,2,c)==1&&otherBad.handle&&!port.owner);
    assert(!malformed.handle&&malformed.refs==1&&txq.credit==1&&port.tx.empty());
    assert(port.bind(&owner,&pool,&txq,&rxq)&&port.open(1,local,c.nowNs,c.deadlineNs));
    Object *packets[]={&a,&b,&d};
    assert(port.offer(&owner,&txq,packets,3,c)==1);assert(a.refs==2&&b.refs==1&&d.refs==1);
    DataTransfer transfer;invalid=c;invalid.backendReady=false;assert(!port.take(invalid,transfer));
    assert(port.take(c,transfer));port.stop(false,-1);assert(!port.unbind()&&a.handle);
    int diagnostic=-999;assert(port.finish(transfer.ticket,0,diagnostic)&&diagnostic==0);
    assert(!port.finish(transfer.ticket,0,diagnostic)&&txq.credit==2&&pool.refs==1);assert(port.unbind());
    assert(port.bind(&owner,&pool,&txq,&rxq));assert(port.open(1,local,c.nowNs,c.deadlineNs));
    // RX validates a known peer/window before allocating any native object.
    auto incoming=e;memcpy(incoming.data(),local,6);memcpy(incoming.data()+6,peer.address,6);
    uint8_t wire[kAwdlDataMax];size_t n;
    auto receive=[&](uint16_t sequence){
        assert(encodeData(incoming.data(),incoming.size(),peer.address,0,sequence,wire,sizeof(wire),n)==DataError::Ok);
        return port.receive(wire,n,c);
    };
    assert(receive(65535));assert(!receive(65535));Object *out[2]={};
    failStage=1;assert(!port.poll(&owner,&rxq,out,2,c)&&port.rxCount==1);failStage=0;
    roc.phase=RocPhase::Draining;assert(port.poll(&owner,&rxq,out,1,c)==1);assert(recycleNativeDataReceive(out[0]));roc.phase=RocPhase::Running;
    assert(receive(0));assert(receive(65534));assert(!receive(65534));
    assert(port.poll(&owner,&rxq,out,1,c)==1);assert(recycleNativeDataReceive(out[0]));
    assert(port.poll(&owner,&rxq,out,1,c)==1);assert(recycleNativeDataReceive(out[0]));
    assert(receive(1));assert(receive(2));assert(receive(3));assert(receive(4));assert(!receive(5));
    assert(port.poll(&owner,&rxq,out,1,c)==1);assert(recycleNativeDataReceive(out[0]));assert(receive(5));
    ++c.modelEpoch;assert(!receive(5)&&port.rxCount==0&&!port.tx.enabled);
    assert(port.open(1,local,c.nowNs,c.deadlineNs));assert(receive(5)&&port.rxCount==1);
    assert(port.poll(&owner,&rxq,out,1,c)==1);assert(recycleNativeDataReceive(out[0]));
    for(unsigned i=0;i<18;++i){++peer.address[5];memcpy(incoming.data()+6,peer.address,6);
        assert(receive(6));assert(port.poll(&owner,&rxq,out,1,c)==1);assert(recycleNativeDataReceive(out[0]));}
    DataTicket oldEpoch;NativeDataOps<Object>::retain(&d,&txq);
    assert(port.tx.push(e.data(),e.size(),1,c.nowNs,0,0,&d,&txq,oldEpoch));
    ++c.modelEpoch;assert(!port.ready(c)&&!port.tx.enabled&&!d.handle&&port.tx.empty());
    port.stop(false,-1);assert(port.rxCount==0&&port.unbind()&&pool.refs==1);
    assert(port.bind(&owner,&pool,&txq,&rxq));assert(port.open(1,local,c.nowNs,c.deadlineNs));
    Object expired("IOSkywalkNetworkPacket");expired.pool=&pool;expired.source=&txq;
    NativeDataOps<Object>::retain(&expired,&txq);
    assert(port.tx.push(e.data(),e.size(),1,c.nowNs,0,0,&expired,&txq,oldEpoch));
    port.expire(1,c.deadlineNs-1,-1);assert(expired.handle&&port.tx.enabled);
    port.expire(1,c.deadlineNs,-1);assert(!expired.handle&&!port.tx.enabled&&port.unbind()&&pool.refs==1);
    assert(nativeDataQueuesReady(&txq,&rxq));
    rxq.workloop=nullptr;assert(!nativeDataQueuesReady(&txq,&rxq));rxq.workloop=&rxq;
    txq.queueEnabled=false;assert(!nativeDataQueuesReady(&txq,&rxq));txq.queueEnabled=true;
    assert(!nativeDataQueuesReady(&pool,&rxq));
    // Registration identity is immutable for a bound port and independent of STA MAC.
    {
     const uint8_t awdl[6]={0x16,1,2,3,4,6};uint8_t chosen[6];DataPort<Object> identity;
     assert(!selectLocalDataIdentity(true,local,awdl,false,chosen));
     assert(selectLocalDataIdentity(false,local,nullptr,false,chosen)&&!memcmp(chosen,local,6));
     assert(!identity.bind(&owner,&pool,&txq,&rxq,multicast)&&!identity.owner);
     assert(identity.bind(&owner,&pool,&txq,&rxq,awdl));
     assert(selectLocalDataIdentity(true,local,identity.registeredLocal,true,chosen)&&!memcmp(chosen,awdl,6));
     assert(!identity.bind(&owner,&pool,&txq,&rxq,local)&&!memcmp(identity.registeredLocal,awdl,6));
     assert(identity.unbind()&&!dataUnicast(identity.registeredLocal));
     assert(!selectLocalDataIdentity(true,local,identity.registeredLocal,false,chosen));
    }
    // Bind the software ticket to actual hardware queue/index, not software slot.
    for(unsigned outcome=0;outcome<3;++outcome){
     DataPort<Object> linked;assert(linked.bind(&owner,&pool,&txq,&rxq)&&linked.open(c.generation,local,c.nowNs,c.deadlineNs));
     auto bytes=e;memcpy(bytes.data(),peer.address,6);
     Object first("IOSkywalkNetworkPacket"),second("IOSkywalkNetworkPacket");
     for(auto p:{&first,&second}){p->pool=&pool;p->source=&txq;p->length=bytes.size();memcpy(p->data.data(),bytes.data(),bytes.size());}
     const auto credit=txq.credit;
     Object *pending[]={&first,&second};assert(linked.offer(&owner,&txq,pending,2,c)==2);
     DataTransfer flight;assert(!linked.takeForHardware(c,9,63,flight));
     assert(linked.takeForHardware(c,10,63,flight)&&flight.ticket.slot!=63);
     assert(!linked.take(c,flight));int diagnostic=-999;
     assert(!linked.finishHardware(c.generation+1,10,63,0,diagnostic));
     assert(!linked.finishHardware(c.generation,11,63,0,diagnostic));
     assert(!linked.finishHardware(c.generation,10,62,0,diagnostic));
     assert(!linked.finish(flight.ticket,0,diagnostic)&&first.handle&&first.refs==2);
     assert(!linked.abortBeforeHardwarePublication(false,ECANCELED,diagnostic));
     linked.stop(false,ECANCELED);assert(first.handle&&!second.handle&&txq.credit==credit+1&&!linked.unbind());
     if(outcome==0)assert(linked.finishHardware(c.generation,10,63,0,diagnostic));
     if(outcome==1)linked.stop(true,ECANCELED);
     if(outcome==2)assert(linked.abortBeforeHardwarePublication(true,ECANCELED,diagnostic));
     assert(!linked.finishHardware(c.generation,10,63,0,diagnostic));
     assert(!first.handle&&!second.handle&&txq.credit==credit+2&&pool.refs==1&&linked.unbind());
    }
    puts("PASS data port: real timing audit -> peer/phase/channel checks, closed backend/gate, contiguous TX prefix, completion after stop, RX post-END delivery, bounded backpressure, 16-bit replay window/epoch reset");
}
int main(){
    Object pool("IOSkywalkPacketBufferPool"),otherPool("IOSkywalkPacketBufferPool");
    Object queue("IOSkywalkTxSubmissionQueue"),otherQueue("IOSkywalkTxSubmissionQueue");queue.pool=&pool;otherQueue.pool=&otherPool;
    Object p("IOSkywalkNetworkPacket");p.pool=&pool;p.source=&queue;auto e=eth();
    p.length=e.size();memcpy(p.data.data(),e.data(),e.size());
    DataQueue data;assert(data.open(1,e.data()+6,1,100));
    assert(!offerNativeDataPacket(data,&p,&otherQueue,1,2,0,0));
    p.buffers=2;assert(!offerNativeDataPacket(data,&p,&queue,1,2,0,0));p.buffers=1;
    p.offset=2040;assert(!offerNativeDataPacket(data,&p,&queue,1,2,0,0));p.offset=0;
    p.offset=65536;assert(!offerNativeDataPacket(data,&p,&queue,1,2,0,0));
    p.offset=-1;assert(!offerNativeDataPacket(data,&p,&queue,1,2,0,0));p.offset=0;
    p.missingBytes=true;assert(!offerNativeDataPacket(data,&p,&queue,1,2,0,0));p.missingBytes=false;
    const char *network=p.kind;p.kind="IOSkywalkPacket";assert(!offerNativeDataPacket(data,&p,&queue,1,2,0,0));p.kind=network;
    assert(p.refs==1&&queue.refs==1&&pool.refs==1&&p.handle);
    p.offset=37;memset(p.data.data(),0xa5,p.data.size());memcpy(p.data.data()+37,e.data(),e.size());
    assert(offerNativeDataPacket(data,&p,&queue,1,2,0,0));assert(p.refs==2&&queue.refs==2&&pool.refs==2);
    DataTransfer transfer;assert(data.take(1,3,transfer));DataCompletion done;assert(data.finish(transfer.ticket,done));
    statusResult=-42;reentrant=&done;assert(completeDataPacket<NativeDataOps<Object>>(done,-1)==-42);
    assert(!p.handle&&p.recycled==1&&queue.credit==1&&p.refs==1&&queue.refs==1&&pool.refs==1);
    assert(!done.packet&&!done.queue);completeDataPacket<NativeDataOps<Object>>(done,0);
    assert(p.recycled==1&&queue.credit==1);statusResult=0;
    // Copying a frame did not recycle it. Expiry cannot release DMA-owned work.
    p.handle=p.prepared=true;p.source=&queue;
    assert(offerNativeDataPacket(data,&p,&queue,1,4,0,0));assert(data.take(1,5,transfer));
    data.stop();assert(!data.reclaim(false,done));assert(p.handle&&queue.credit==1);
    assert(data.reclaim(true,done));assert(completeDataPacket<NativeDataOps<Object>>(done,-1)==0);
    assert(queue.credit==2&&p.recycled==2&&pool.refs==1);
    rx.handle=false;
    for(failStage=1;failStage<=6;++failStage){
        badMulti=failStage==3;const auto count=rx.recycled;
        assert(!allocateNativeDataReceive(&pool,e.data(),e.size()));
        assert(!rx.handle&&rx.recycled==count+(failStage!=1));
    }
    failStage=0;badMulti=false;const unsigned before=allocations;
    e[12]=8;assert(!allocateNativeDataReceive(&pool,e.data(),e.size()));assert(allocations==before);e[12]=0x86;
    auto receive=allocateNativeDataReceive(&pool,e.data(),e.size());assert(receive==&rx&&rx.handle&&rx.prepared);
    assert(rx.length==e.size()&&rx.offset==0&&!memcmp(rx.data.data(),e.data(),e.size()));
    assert(rx.data[e.size()]==0xa5);assert(recycleNativeDataReceive(receive));
    assert(!rx.handle&&pool.refs==1&&!receive);assert(!recycleNativeDataReceive(receive));
    Object *invalid=&pool;assert(!recycleNativeDataReceive(invalid));
    portTests();
    puts("PASS native-symbol adapter: type/pool/source, single-buffer bounds, pinned pool, TX recycle+credit once, status failure, DMA quarantine, RX allocation/prepare/range failures");
    return 0;
}
