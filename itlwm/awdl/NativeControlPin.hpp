// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
namespace ItlAwdl {
// Serialized by the interface lifetime lock; separate from DMA/data ownership.
struct NativeControlPin {
 uint64_t epoch=0;bool closing=false;
 bool acquire(uint64_t e){if(!e||epoch||closing)return false;epoch=e;return true;}
 bool release(uint64_t e,bool down){if(!e||e!=epoch||!down)return false;epoch=0;return true;}
 // Only the controller full-stop path may take over a failed control withdrawal.
 bool handoff(uint64_t e){if(!closing||!e||epoch!=e)return false;epoch=0;return true;}
 bool close(){closing=true;return !epoch;}
 bool permits(bool start)const{return !epoch&&(!start||!closing);}
};
}
