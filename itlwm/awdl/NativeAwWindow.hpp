// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include "NativeSyncProfile.hpp"

namespace ItlAwdl {
// Publishes host-scheduled AW boundaries from the same GP2 clock that gates
// real traffic. The native manager uses this sequence while deciding whether
// a peer's real-time indication can be sent.
struct NativeAwWindowTracker {
    bool initialized=false;
    uint32_t lastSlot=0;
    uint16_t number=0;

    bool observe(uint32_t epochGp2,uint32_t nowGp2,uint32_t &startGp2,uint16_t &awNumber){
        if(nowGp2<epochGp2)return false;
        const uint32_t slot=(nowGp2-epochGp2)/NativeSyncProfile::periodUs;
        if(initialized&&slot==lastSlot)return false;
        uint32_t advance=initialized&&slot>lastSlot?slot-lastSlot:1;
        if(advance>65535)advance=65535;
        uint32_t next=uint32_t(number)+advance;
        while(next>65535)next-=65535;
        number=uint16_t(next?next:1);
        lastSlot=slot;
        initialized=true;
        startGp2=epochGp2+slot*NativeSyncProfile::periodUs;
        awNumber=number;
        return true;
    }

    static uint32_t nextBoundaryUs(uint64_t nowNs,uint64_t epochNs){
        constexpr uint64_t periodNs=uint64_t(NativeSyncProfile::periodUs)*1000ULL;
        if(!epochNs||nowNs<epochNs)return 0;
        const uint64_t phase=(nowNs-epochNs)%periodNs;
        const uint64_t waitNs=periodNs-phase;
        return uint32_t((waitNs+999ULL)/1000ULL);
    }
};
}
