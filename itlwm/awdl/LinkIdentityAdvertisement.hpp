// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "SelfControlAdvertisement.hpp"
#include "FixedChannelControl.hpp"
namespace ItlAwdl {
// Fixed-channel diagnostic node, no DNS records or AirDrop service.
// IWM supports MCS0-7. Advertise HT20/one stream, disabled SMPS, 3839-byte
// A-MSDU and 8K A-MPDU/16us spacing; no SGI, LDPC, STBC, HT40 or VHT.
constexpr uint8_t kLocalHtTlv[]={7,8,0,0,0,0x0c,0,0x1c,0xff,0,0};
constexpr size_t kLinkIdentityLength=279+sizeof(kLocalHtTlv);
constexpr size_t kPeriodicPsfLength=kControlActionLength+sizeof(kLocalHtTlv)+5;
inline bool finishLinkIdentity(const uint8_t source[6],uint8_t *out,size_t capacity,size_t n,size_t &used){
 used=0;if(!out||capacity<kLinkIdentityLength||n!=kControlActionLength)return false;
 // Empty Service Parameters; this is peer identity, not an AirDrop service.
 out[n++]=6;actionWrite16(out+n,9);n+=2;memset(out+n,0,9);n+=9;
 memcpy(out+n,kLocalHtTlv,sizeof(kLocalHtTlv));n+=sizeof(kLocalHtTlv);
 // ARPA: one diagnostic label plus the observed .local suffix.
 const char name[]="awdl-probe";out[n++]=16;actionWrite16(out+n,14);n+=2;
 out[n++]=3;out[n++]=10;memcpy(out+n,name,10);n+=10;out[n++]=0xc0;out[n++]=0x0c;
 // Only the fields present: country, social-channel map, AWDL address.
 out[n++]=12;actionWrite16(out+n,15);n+=2;actionWrite16(out+n,0x0304);n+=2;
 out[n++]='X';out[n++]='0';out[n++]=0;actionWrite16(out+n,1);n+=2;
 memcpy(out+n,source,6);n+=6;actionWrite16(out+n,0);n+=2;
 out[n++]=21;actionWrite16(out+n,2);n+=2;out[n++]=0x34;out[n++]=1;
 used=n;return n==kLinkIdentityLength;
}
inline bool buildLinkIdentity(const SelfControlInput &input,uint8_t *out,size_t capacity,size_t &used){
 used=0;if(!out||capacity<kLinkIdentityLength||input.subtype!=3)return false;
 uint8_t base[kControlActionLength];size_t n=0;SelfControlEvidence proof;
 if(!buildSelfControlAdvertisement(input,base,sizeof(base),n,proof))return false;
 memcpy(out,base,n);
 return finishLinkIdentity(input.source,out,capacity,n,used);
}

inline bool buildFollowingLinkIdentity(const SelfControlInput &input,const FixedChannelControl &node,
 uint8_t *out,size_t capacity,size_t &used,FixedControlView &proof){
 used=0;proof={};if(!out||capacity<kLinkIdentityLength||input.subtype!=3)return false;
 uint8_t base[kControlActionLength];size_t n=0;SelfControlEvidence bounds;
 // Retain the established host/GP2/lease validation, then encode the selected tree.
 if(!buildSelfControlAdvertisement(input,base,sizeof(base),n,bounds))return false;
 if(!node.build(input.generation,input.nowGp2,input.nowHostNs,3,input.sequence,base,sizeof(base),n,proof)||
    memcmp(base+10,input.source,6))return false;
 memcpy(out,base,n);return finishLinkIdentity(input.source,out,capacity,n,used);
}

inline bool buildFollowingPeriodicControl(const SelfControlInput &input,const FixedChannelControl &node,
 uint8_t *out,size_t capacity,size_t &used,FixedControlView &proof){
 if(input.subtype==3)return buildFollowingLinkIdentity(input,node,out,capacity,used,proof);
 used=0;proof={};if(input.subtype!=0||!out||capacity<kPeriodicPsfLength)return false;
 uint8_t base[kControlActionLength];size_t n=0;SelfControlEvidence bounds;
 if(!buildSelfControlAdvertisement(input,base,sizeof(base),n,bounds)||
    !node.build(input.generation,input.nowGp2,input.nowHostNs,0,input.sequence,base,sizeof(base),n,proof)||
    memcmp(base+10,input.source,6))return false;
 memcpy(out,base,n);memcpy(out+n,kLocalHtTlv,sizeof(kLocalHtTlv));n+=sizeof(kLocalHtTlv);
 out[n++]=21;actionWrite16(out+n,2);n+=2;out[n++]=0x34;out[n++]=1;
 used=n;return true;
}

}
