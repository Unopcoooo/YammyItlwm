// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace ItlAwdl {
// Only the exact observed24H32 local-address request. This is not role/RF authority.
inline unsigned nativeLocalSyncRequest(const void *payload,size_t length,const uint8_t local[6]){
 if(!payload||length!=24||!local)return 1;
 auto p=static_cast<const uint8_t*>(payload);
 const auto word=[&](unsigned n){return uint32_t(p[n])|uint32_t(p[n+1])<<8|uint32_t(p[n+2])<<16|uint32_t(p[n+3])<<24;};
 if(word(0)!=0||word(4)!=1||word(16)!=1)return 2;
 if(p[14]||p[15]||p[20]||p[21]||p[22]||p[23])return 3;
 uint8_t any=0;for(unsigned n=0;n<6;++n)any|=local[n];
 if(!any||(local[0]&1)||memcmp(p+8,local,6))return 4;
 return 0;
}
}
