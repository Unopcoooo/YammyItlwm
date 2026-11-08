// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "DataQueue.hpp"
namespace ItlAwdl {
// Sequoia KC-verified direct symbols; no reconstructed Skywalk vtable or layout.
extern "C" uint32_t dataNativeBufferCount(const void *)
    __asm__("__ZNK15IOSkywalkPacket20getPacketBufferCountEv");
extern "C" uint32_t dataNativeLength(const void *)
    __asm__("__ZNK15IOSkywalkPacket13getDataLengthEv");
extern "C" uint32_t dataNativeBufferSize(const void *)
    __asm__("__ZNK15IOSkywalkPacket13getBufferSizeEv");
extern "C" int64_t dataNativeOffset(const void *)
    __asm__("__ZNK15IOSkywalkPacket10getDataOffEv");
extern "C" uint8_t *dataNativeBufferBase(const void *)
    __asm__("__ZNK15IOSkywalkPacket21getDataVirtualAddressEv");
extern "C" void *dataNativePacketPool(const void *)
    __asm__("__ZNK15IOSkywalkPacket19getPacketBufferPoolEv");
extern "C" void *dataNativeQueuePool(const void *)
    __asm__("__ZNK20IOSkywalkPacketQueue19getPacketBufferPoolEv");
extern "C" void *dataNativeSource(const void *)
    __asm__("__ZNK15IOSkywalkPacket14getSourceQueueEv");
extern "C" int dataNativeStatus(void *,int)
    __asm__("__ZN22IOSkywalkNetworkPacket19setCompletionStatusEi");
// The KC null branch defines no return value. It completes prepared packets, reports TX
// queue credit, disposes the wrapper and frees the packet handle, in that order.
extern "C" void dataNativeRecycle(void *,void *)
    __asm__("__ZN25IOSkywalkPacketBufferPool16deallocatePacketEP15IOSkywalkPacket");
extern "C" int dataNativeAllocate(void *,uint32_t,void **,uint32_t)
    __asm__("__ZN25IOSkywalkPacketBufferPool14allocatePacketEjPP15IOSkywalkPacketj");
extern "C" int dataNativePrepare(void *,void *,uint32_t,uint32_t)
    __asm__("__ZN15IOSkywalkPacket16prepareWithQueueEP20IOSkywalkPacketQueuejj");
extern "C" int dataNativeSetRange(void *,uint16_t,uint32_t)
    __asm__("__ZN15IOSkywalkPacket22setDataOffsetAndLengthEtj");
extern "C" int dataNativeRequestReceive(void *,void *,uint32_t)
    __asm__("__ZN26IOSkywalkRxCompletionQueue14requestEnqueueEPvj");
extern "C" int dataNativeRequestTransmit(void *,void *,uint32_t)
    __asm__("__ZN26IOSkywalkTxSubmissionQueue14requestDequeueEPvj");
template<class Object> int notifyNativeDataTransmit(Object *queue){
    if(!queue||!queue->metaCast("IOSkywalkTxSubmissionQueue"))return -1;
    return dataNativeRequestTransmit(queue,nullptr,1);
}
extern "C" void *dataNativeWorkLoop(const void *)
    __asm__("__ZNK13IOEventSource11getWorkLoopEv");
extern "C" bool dataNativeQueueEnabled(const void *)
    __asm__("__ZNK13IOEventSource9isEnabledEv");
template<class Object> bool nativeDataQueuesReady(Object *tx,Object *rx){
    return tx&&rx&&tx->metaCast("IOSkywalkTxSubmissionQueue")&&rx->metaCast("IOSkywalkRxCompletionQueue")&&
        dataNativeWorkLoop(tx)&&dataNativeWorkLoop(rx)&&dataNativeQueueEnabled(tx)&&dataNativeQueueEnabled(rx);
}
template<class Object> int notifyNativeDataReceive(Object *queue){
    if(!queue||!queue->metaCast("IOSkywalkRxCompletionQueue"))return -1;
    // Sequoia KC: option bit 0 schedules signalWorkAvailable; option 0 enters
    // the queue gate and calls our poll callback synchronously. Avoid gate inversion.
    return dataNativeRequestReceive(queue,nullptr,1);
}

// Object is OSObject in kernel builds. A small fake exercises these same direct
// calls in host tests. Caller must serialize all operations with the main gate.
template<class Object> struct NativeDataOps {
    using Packet=Object;using Queue=Object;
    static bool valid(Packet *p,Queue *q){
        if(!p||!q||!p->metaCast("IOSkywalkNetworkPacket")||!q->metaCast("IOSkywalkTxSubmissionQueue"))return false;
        auto pool=static_cast<Object *>(dataNativePacketPool(p));
        return pool&&pool->metaCast("IOSkywalkPacketBufferPool")&&
            dataNativeQueuePool(q)==pool&&dataNativeSource(p)==q;
    }
    static unsigned bufferCount(Packet *p){return dataNativeBufferCount(p);}
    static size_t length(Packet *p){return dataNativeLength(p);}
    static const uint8_t *bytes(Packet *p){
        const uint32_t size=dataNativeBufferSize(p),n=dataNativeLength(p);
        const int64_t offset=dataNativeOffset(p);
        if(offset<0||uint64_t(offset)>size||n>size-uint32_t(offset))return nullptr;
        // Native virtual address is the buffer base, not the data start.
        const auto base=dataNativeBufferBase(p);return base?base+size_t(offset):nullptr;
    }
    static void retain(Packet *p,Queue *q){
        static_cast<Object *>(dataNativePacketPool(p))->retain();p->retain();q->retain();
    }
    static void release(Packet *p,Queue *q){
        auto pool=static_cast<Object *>(dataNativePacketPool(p));
        p->release();q->release();pool->release();
    }
    // A status setter failure does not transfer ownership back. Recycle still
    // consumes the handle; report the error to the caller, never retry recycling.
    static int complete(Packet *p,Queue *,int status){
        const int result=dataNativeStatus(p,status);
        dataNativeRecycle(dataNativePacketPool(p),p);return result;
    }
};
template<class Object> bool offerNativeDataPacket(DataQueue &q,Object *p,Object *source,
                                                uint32_t gen,uint64_t now,uint16_t wifiSeq,uint16_t awdlSeq){
    if(!q.ready(gen,now)||!NativeDataOps<Object>::valid(p,source))return false;
    return offerDataPacket<NativeDataOps<Object>>(q,p,source,gen,now,wifiSeq,awdlSeq);
}
template<class Object> bool recycleNativeDataReceive(Object *&p){
    if(!p||!p->metaCast("IOSkywalkPacket"))return false;
    auto pool=static_cast<Object *>(dataNativePacketPool(p));
    if(!pool||!pool->metaCast("IOSkywalkPacketBufferPool"))return false;
    auto packet=p;p=nullptr;dataNativeRecycle(pool,packet);return true;
}
// The Ethernet input must already have passed decodeData with a fresh known
// peer and the current RX window. Allocation never sleeps and cannot set RUNNING.
// Caller keeps the registered pool alive until delivery or recycling completes.
template<class Object> Object *allocateNativeDataReceive(Object *pool,const uint8_t *eth,size_t n){
    if(!pool||!pool->metaCast("IOSkywalkPacketBufferPool")||!eth||n<54||n>kEtherDataMax||
       !dataUnicast(eth+6)||dataBe16(eth+12)!=0x86dd||!dataIpv6(eth+14,n-14)||
       !dataDestination(eth,eth+14))return nullptr;
    void *raw=nullptr;
    if(dataNativeAllocate(pool,1,&raw,1)||!raw)return nullptr;
    auto p=static_cast<Object *>(raw);
    if(!p->metaCast("IOSkywalkNetworkPacket")||dataNativePacketPool(p)!=pool||
       dataNativePrepare(p,nullptr,0,0)||dataNativeBufferCount(p)!=1||
       dataNativeBufferSize(p)<n||dataNativeSetRange(p,0,uint32_t(n))){
        recycleNativeDataReceive(p);return nullptr;
    }
    auto bytes=dataNativeBufferBase(p);
    if(!bytes){recycleNativeDataReceive(p);return nullptr;}
    memcpy(bytes,eth,n);return p;
}
}
