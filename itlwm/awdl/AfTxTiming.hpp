// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "ActionFrame.hpp"
namespace ItlAwdl {
// Wire observations only. Peer PHY/target clocks never authorize scheduling.
struct AfTxTiming { uint32_t phy=0,target=0,sequence=0; };
inline uint32_t afTxRead32(const uint8_t *p) {
    return uint32_t(p[0]) | (uint32_t(p[1])<<8) | (uint32_t(p[2])<<16) | (uint32_t(p[3])<<24);
}
inline bool decodeAfTxTiming(const uint8_t *p,size_t n,AfTxTiming &out) {
    out={}; Summary envelope;
    if(inspect(p,n,envelope)!=Result::Valid)return false;
    const size_t header=24+((p[1]&0x80)?4:0);
    AfTxTiming candidate;
    candidate.phy=afTxRead32(p+header+8);candidate.target=afTxRead32(p+header+12);
    candidate.sequence=uint32_t(p[22]) | (uint32_t(p[23])<<8);
    out=candidate;return true;
}
} // namespace ItlAwdl
