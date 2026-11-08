// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <IOKit/IOEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include "DataNative.hpp"
namespace ItlAwdl {
// These Sequoia methods are void. Read enabled/workloop state after each call.
extern "C" void nativeTxQueueEnable(void *) __asm__("__ZN26IOSkywalkTxSubmissionQueue6enableEv");
extern "C" void nativeRxQueueEnable(void *) __asm__("__ZN26IOSkywalkRxCompletionQueue6enableEv");
extern "C" void nativeTxQueueDisable(void *) __asm__("__ZN26IOSkywalkTxSubmissionQueue7disableEv");
extern "C" void nativeRxQueueDisable(void *) __asm__("__ZN26IOSkywalkRxCompletionQueue7disableEv");
struct NativeQueueOps {
 IOWorkLoop *loop;
 bool valid(OSObject *q,bool tx){return q&&OSDynamicCast(IOEventSource,q)&&
  q->metaCast(tx?"IOSkywalkTxSubmissionQueue":"IOSkywalkRxCompletionQueue");}
 void *workloop(OSObject *q){return dataNativeWorkLoop(q);}
 bool enabled(OSObject *q){return dataNativeQueueEnabled(q);}
 bool attach(OSObject *q){auto event=OSDynamicCast(IOEventSource,q);return event&&loop->addEventSource(event)==kIOReturnSuccess;}
 bool detach(OSObject *q){auto event=OSDynamicCast(IOEventSource,q);return event&&loop->removeEventSource(event)==kIOReturnSuccess;}
 void enable(OSObject *q,bool tx){if(tx)nativeTxQueueEnable(q);else nativeRxQueueEnable(q);}
 void disable(OSObject *q,bool tx){if(tx)nativeTxQueueDisable(q);else nativeRxQueueDisable(q);}
};
}
