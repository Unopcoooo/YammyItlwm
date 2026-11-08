// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "DataFrames.hpp"
namespace ItlAwdl {
// Never silently substitute the infrastructure identity for a native AWDL session.
inline bool selectLocalDataIdentity(bool native,const uint8_t *ordinary,const uint8_t *registered,
                                    bool bound,uint8_t out[6]){
 if(!out)return false;
 const auto address=native?(bound?registered:nullptr):ordinary;
 if(!address||!dataUnicast(address)){memset(out,0,6);return false;}
 memcpy(out,address,6);return true;
}
}
