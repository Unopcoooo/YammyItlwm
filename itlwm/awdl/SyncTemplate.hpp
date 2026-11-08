// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <string.h>
namespace ItlAwdl {
constexpr uint32_t kSyncTemplateCapacity = 512;
struct SyncTemplateState {
    uint32_t sequence;
    uint32_t length;
    uint32_t revision;
    uint8_t bytes[kSyncTemplateCapacity];
};
extern SyncTemplateState gSyncTemplate;
inline bool syncTemplateFrameUsable(const uint8_t *frame, uint32_t length) {
    if (!frame || length < 24 || length > kSyncTemplateCapacity) return false;
    const uint16_t fc = uint16_t(frame[0]) | (uint16_t(frame[1]) << 8);
    return (fc & 0x0c) == 0 && (fc & 0xf0) == 0xd0;
}
inline bool publishSyncTemplate(const uint8_t *frame, uint32_t length) {
    if ((!frame && length) || length > kSyncTemplateCapacity) return false;
    uint32_t expected=0;
    if(!__atomic_compare_exchange_n(&gSyncTemplate.sequence,&expected,1,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return false;
    if(length)memcpy(gSyncTemplate.bytes,frame,length);
    gSyncTemplate.length=length;++gSyncTemplate.revision;
    __atomic_store_n(&gSyncTemplate.sequence,0,__ATOMIC_RELEASE);
    return true;
}
inline bool snapshotSyncTemplate(uint8_t *out, uint32_t capacity, uint32_t &length,uint32_t *revision=nullptr) {
    if (!out || capacity < kSyncTemplateCapacity) return false;
    uint32_t expected=0;
    if(!__atomic_compare_exchange_n(&gSyncTemplate.sequence,&expected,1,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return false;
    length=gSyncTemplate.length;if(revision)*revision=gSyncTemplate.revision;
    bool valid=length>0&&length<=capacity;
    if(valid)memcpy(out,gSyncTemplate.bytes,length);
    __atomic_store_n(&gSyncTemplate.sequence,0,__ATOMIC_RELEASE);
    return valid;
}
inline bool syncTemplateRevisionMatches(uint32_t revision) {
    uint32_t expected=0;
    if(!__atomic_compare_exchange_n(&gSyncTemplate.sequence,&expected,1,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return false;
    bool matches=gSyncTemplate.revision==revision&&gSyncTemplate.length>0;
    __atomic_store_n(&gSyncTemplate.sequence,0,__ATOMIC_RELEASE);return matches;
}

}
