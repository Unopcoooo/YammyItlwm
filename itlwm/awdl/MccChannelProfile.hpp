// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
namespace ItlIbss {
// Offline decoder only. No I/O, country selection, map mutation or RF authority.
struct MccChannelProfile {
    bool decoded=false;uint32_t generation=0,status=0,count=0;
    uint16_t country=0;uint8_t source=0,present=0;
    uint32_t flags6=0,flags44=0,flags149=0;
    bool accepted()const{return decoded && status<=1;}
    bool flags(unsigned channel,uint32_t &out)const {
        out=0;const unsigned bit=channel==6?1:channel==44?2:channel==149?4:0;
        if(!bit || !(present&bit))return false;
        out=channel==6?flags6:channel==44?flags44:flags149;return true;
    }
    bool permitsActiveIbss(unsigned channel,uint32_t currentGeneration)const {
        uint32_t f=0;
        return currentGeneration && generation==currentGeneration && accepted() && flags(channel,f) &&
            (f&0xb)==0xb && !(f&0x90); // local VALID/IBSS/ACTIVE; RADAR/DFS reject.
    }
};
inline uint32_t mccRead32(const uint8_t *p) {
    return uint32_t(p[0]) | (uint32_t(p[1])<<8) | (uint32_t(p[2])<<16) | (uint32_t(p[3])<<24);
}
// Map/count must be from the exact native platform table, not a guessed prefix.
inline MccChannelProfile decodeMccChannelProfile(const uint8_t *raw,size_t length,bool version3,
                                                 const uint8_t *map,size_t mapCount,uint32_t generation) {
    MccChannelProfile result;const size_t header=version3?16:12;
    if(!raw || !map || !generation || !mapCount || mapCount>64 || length<header)return result;
    const auto count=mccRead32(raw+(version3?12:8));
    if(count!=mapCount || count>64 || length!=header+size_t(count)*4)return result;
    for(size_t i=0;i<mapCount;++i) {
        if(!map[i])return result;
        for(size_t j=0;j<i;++j)if(map[j]==map[i])return result;
    }
    result.status=mccRead32(raw);result.country=uint16_t(raw[4])|(uint16_t(raw[5])<<8);
    result.source=raw[7];result.generation=generation;result.count=count;
    for(size_t i=0;i<mapCount;++i) {
        const auto f=mccRead32(raw+header+i*4);
        if(map[i]==6){result.flags6=f;result.present|=1;}
        if(map[i]==44){result.flags44=f;result.present|=2;}
        if(map[i]==149){result.flags149=f;result.present|=4;}
    }
    result.decoded=true;return result;
}
}
