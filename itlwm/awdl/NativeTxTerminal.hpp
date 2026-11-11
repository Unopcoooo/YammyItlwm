// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
namespace ItlAwdl {
// A deadline marker means the driver requested a flush, not that the frame
// failed. Accept only a fully reclaimed success as a late-but-terminal frame.
inline bool nativeTxLateSuccess(bool done,bool held,bool pending,bool ownerMismatch,
 uint32_t txGeneration,uint32_t generation,uint32_t completed,uint32_t reclaimed,
 uint32_t stopReclaimed,uint32_t deadlineExpired,uint32_t flushFailed,
 uint32_t cancelled,uint32_t mismatch,uint32_t status){
 return !done&&!held&&!pending&&!ownerMismatch&&txGeneration==generation&&completed==1&&
  reclaimed==1&&!stopReclaimed&&deadlineExpired==1&&!flushFailed&&!cancelled&&!mismatch&&
  (status&255)==1;
}
}
