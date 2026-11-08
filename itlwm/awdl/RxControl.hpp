// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
namespace ItlAwdl {
// Conservative, non-DFS home-channel subset. Runtime must additionally validate
// the actual associated channel, regulatory flags and unchanged association.
inline bool rxControlHomeAllowed(uint32_t c) {
    return (c>=1 && c<=14) || c==36 || c==40 || c==44 || c==48 ||
           c==149 || c==153 || c==157 || c==161 || c==165;
}
inline bool rxControlChannel(uint32_t home,uint32_t attempts,uint32_t &out) {
    out=0;
    if(!rxControlHomeAllowed(home) || home==6 || attempts>=24)return false;
    out=((attempts/4)%2)==0 ? home : 6;
    return true;
}
// Preserve the latest firmware interval after queueDrained clears live state.
// This is only a trace-retention rule, never permission to transmit.
struct ReceiveWindow {
    uint32_t generation=0,channel=0,start=0,end=0,duration=0;
    bool valid=false,ended=false;
    bool contains(uint32_t epoch,uint32_t ch,uint32_t gp2) const {
        if(!valid || epoch!=generation || ch!=channel || !duration || duration>65536)return false;
        const uint32_t span=ended ? end-start : duration;
        return span && span<=duration && uint32_t(gp2-start)<span;
    }
};
}
