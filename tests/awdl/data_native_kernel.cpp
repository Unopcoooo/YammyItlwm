#include <libkern/c++/OSObject.h>
#include "DataNative.hpp"
#include "DataPort.hpp"
bool nativeHardwareTake(ItlAwdl::DataPort<OSObject> &port,const ItlAwdl::DataContext &context,ItlAwdl::DataTransfer &frame){
 return port.takeForHardware(context,10,63,frame);
}
bool nativeHardwareDone(ItlAwdl::DataPort<OSObject> &port,uint32_t gen,unsigned q,unsigned index,int &diagnostic){
 return port.finishHardware(gen,q,index,0,diagnostic);
}
int nativeNotifyReceive(ItlAwdl::DataPort<OSObject> &port){return port.notifyReceive();}
bool nativeOffer(ItlAwdl::DataQueue &q,OSObject *packet,OSObject *queue,uint32_t gen,uint64_t now){
    return ItlAwdl::offerNativeDataPacket(q,packet,queue,gen,now,0,0);
}
int nativeDone(ItlAwdl::DataCompletion &done,int status){return ItlAwdl::completeDataPacket<ItlAwdl::NativeDataOps<OSObject>>(done,status);}
OSObject *nativeReceive(OSObject *pool,const uint8_t *eth,size_t n){return ItlAwdl::allocateNativeDataReceive(pool,eth,n);}
bool nativeDrop(OSObject *&packet){return ItlAwdl::recycleNativeDataReceive(packet);}

bool nativeQueueReadiness(OSObject *tx,OSObject *rx){return ItlAwdl::nativeDataQueuesReady(tx,rx);}

int nativeNotifyTransmit(ItlAwdl::DataPort<OSObject> &port){return port.notifyTransmit();}
