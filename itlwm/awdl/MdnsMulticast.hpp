// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "DataFrames.hpp"
namespace ItlAwdl {
// Narrow discovery output only. No extension headers, fragments, legacy queries,
// unicast responses or zero-checksum IPv6 UDP are admitted by this first path.
inline bool mdnsMulticastPayload(const uint8_t *dst,const uint8_t *ip,size_t n,bool nativeDiscoveryRx=false){
 if(!dst||!ip||!dataIpv6(ip,n)||n<60||ip[6]!=17||ip[7]!=255||
    ip[8]!=0xfe||(ip[9]&0xc0)!=0x80)return false;
 const uint8_t mac[]={0x33,0x33,0,0,0,0xfb};
 const uint8_t group[]={0xff,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0xfb};
 if(memcmp(ip+24,group,16)||(memcmp(dst,mac,6)&&!(nativeDiscoveryRx&&nativeMdnsGroup(dst,ip))))return false;
 const auto udp=ip+40;const size_t bytes=n-40;
 if(dataBe16(udp)!=5353||dataBe16(udp+2)!=5353||dataBe16(udp+4)!=bytes||!dataBe16(udp+6))return false;
 uint32_t sum=uint32_t(bytes)+17;
 for(unsigned i=8;i<40;i+=2)sum+=dataBe16(ip+i);
 for(size_t i=0;i<bytes;i+=2)sum+=uint16_t(uint16_t(udp[i])<<8)|(i+1<bytes?udp[i+1]:0);
 while(sum>>16)sum=(sum&0xffff)+(sum>>16);
 return sum==0xffff;
}
inline bool mdnsMulticastEthernet(const uint8_t *eth,size_t n,const uint8_t local[6]){
 return validateDataEthernet(eth,n,local)==DataError::Ok&&mdnsMulticastPayload(eth,eth+14,n-14);
}
inline bool mdnsMulticastFrame(const uint8_t *frame,size_t n){
 // Reuse the actual AWDL data envelope validator; no new wire format.
 return frame&&n>=100&&dataPlainFrameValid(frame,n)&&mdnsMulticastPayload(frame+4,frame+40,n-40);
}
}
