// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "LinkIdentityAdvertisement.hpp"
namespace ItlAwdl {
// Reuse the calibrated IWM capability bytes, without the diagnostic hostname
// or empty service TLV. NativeMifService owns the system hostname and services.
inline bool appendNativeLinkAdvertisement(uint8_t *frame,size_t capacity,size_t &used){
 if(!frame||used!=kControlActionLength||used>capacity||
    (frame[30]!=0&&frame[30]!=3)||!actionUnicastAddress(frame+10))return false;
 const bool mif=frame[30]==3;
 const size_t extra=sizeof(kLocalHtTlv)+5+(mif?18:0);
 if(capacity-used<extra)return false;
 size_t n=used;memcpy(frame+n,kLocalHtTlv,sizeof(kLocalHtTlv));n+=sizeof(kLocalHtTlv);
 if(mif){
  frame[n++]=12;actionWrite16(frame+n,15);n+=2;actionWrite16(frame+n,0x0304);n+=2;
  frame[n++]='X';frame[n++]='0';frame[n++]=0;actionWrite16(frame+n,1);n+=2;
  memcpy(frame+n,frame+10,6);n+=6;actionWrite16(frame+n,0);n+=2;
 }
 frame[n++]=21;actionWrite16(frame+n,2);n+=2;frame[n++]=0x34;frame[n++]=1;
 used=n;return true;
}
}
