// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "MccChannelProfile.hpp"
namespace ItlIbss {
// A return to the verified channel6 does not bypass current native restrictions.
// This policy does not send a command or change country/channel maps.
constexpr uint32_t Channel6NativeRequired=0x10080u;
constexpr uint32_t Channel6NativeDenied=0x280200u;
inline bool channel6ProfileAllowed(uint32_t number,uint32_t frequency,uint32_t native,
                                  uint32_t nvm,bool nvmObserved,
                                  const MccChannelProfile &mcc,uint32_t generation) {
    return number==6 && frequency==2437 && nvmObserved &&
        (native&Channel6NativeRequired)==Channel6NativeRequired && !(native&Channel6NativeDenied) &&
        (nvm&0xb)==0xb && !(nvm&0x90) && mcc.permitsActiveIbss(6,generation);
}
}
