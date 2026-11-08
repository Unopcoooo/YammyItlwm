// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef KERNEL
#include <libkern/libkern.h>
#else
#include <string.h>
#endif
namespace ItlAwdl {
// Wire layout cross-checked with OWL tx.c/rx.c. No radiotap or FCS here.
constexpr size_t kDataMtu=1500, kEtherDataMax=14+kDataMtu, kAwdlDataMax=40+kDataMtu;
enum class DataError { Ok, Length, Address, Protocol, Layout, Peer, Capacity, Alias };
inline uint16_t dataBe16(const uint8_t *p){return uint16_t(uint16_t(p[0])<<8|p[1]);}
inline uint16_t dataLe16(const uint8_t *p){return uint16_t(uint16_t(p[0])|uint16_t(p[1])<<8);}
inline bool dataUnicast(const uint8_t *p){
    if(!p||(p[0]&1))return false;
    uint8_t v=0;for(unsigned i=0;i<6;++i)v|=p[i];return v!=0;
}
inline bool dataOverlap(const uint8_t *a,size_t an,const uint8_t *b,size_t bn){
    const uintptr_t x=reinterpret_cast<uintptr_t>(a),y=reinterpret_cast<uintptr_t>(b);
    return x<=y?y-x<an:x-y<bn;
}
inline bool dataIpv6(const uint8_t *p,size_t n){
    return n>=40&&n<=kDataMtu&&(p[0]>>4)==6&&size_t(dataBe16(p+4))+40==n&&p[8]!=0xff;
}
inline bool dataDestination(const uint8_t *mac,const uint8_t *ip){
    if(ip[24]!=0xff)return dataUnicast(mac);
    return mac[0]==0x33&&mac[1]==0x33&&!memcmp(mac+2,ip+36,4);
}
// The native AWDL interface also uses this exact link-layer group for ff02::fb.
inline bool nativeMdnsGroup(const uint8_t *mac,const uint8_t *ip){
    const uint8_t link[]={0x33,0x33,0x80,0,0,0xfb},group[]={0xff,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0xfb};
    return !memcmp(mac,link,6)&&!memcmp(ip+24,group,16);
}
inline DataError validateDataEthernet(const uint8_t *eth,size_t n,const uint8_t local[6]){
    if(!eth||n<54||n>kEtherDataMax)return DataError::Length;
    if(!dataUnicast(local)||memcmp(eth+6,local,6))return DataError::Address;
    if(dataBe16(eth+12)!=0x86dd||!dataIpv6(eth+14,n-14))return DataError::Protocol;
    if(!dataDestination(eth,eth+14))return DataError::Address;
    return DataError::Ok;
}
inline DataError encodeData(const uint8_t *eth,size_t n,const uint8_t local[6],
                            uint16_t wifiSequence,uint16_t awdlSequence,
                            uint8_t *out,size_t cap,size_t &used){
    used=0;if(!out)return DataError::Length;
    const auto validation=validateDataEthernet(eth,n,local);if(validation!=DataError::Ok)return validation;
    if(wifiSequence>4095)return DataError::Layout;
    const size_t size=n+26;
    if(cap<size)return DataError::Capacity;
    if(dataOverlap(eth,n,out,size)||dataOverlap(local,6,out,size))return DataError::Alias;
    const uint8_t bssid[6]={0,0x25,0,0xff,0x94,0x73};
    const uint8_t llc[8]={0xaa,0xaa,3,0,0x17,0xf2,8,0};
    memset(out,0,40);out[0]=8;
    memcpy(out+4,eth,6);memcpy(out+10,local,6);memcpy(out+16,bssid,6);
    const uint16_t seq=uint16_t(wifiSequence<<4);out[22]=uint8_t(seq);out[23]=uint8_t(seq>>8);
    memcpy(out+24,llc,8);out[32]=3;out[33]=4;
    out[34]=uint8_t(awdlSequence);out[35]=uint8_t(awdlSequence>>8);
    out[38]=0x86;out[39]=0xdd;memcpy(out+40,eth+14,n-14);used=size;
    return DataError::Ok;
}
inline DataError decodeData(const uint8_t *frame,size_t n,const uint8_t local[6],
                            const uint8_t peer[6],uint8_t *out,size_t cap,size_t &used,
                            uint16_t &awdlSequence,bool nativeDiscoveryRx=false){
    used=0;awdlSequence=0;
    if(!frame||!out||n<80||n>kAwdlDataMax+2+(nativeDiscoveryRx?8:0))return DataError::Length;
    const uint16_t fc=dataLe16(frame);
    // Only 3-address nonfragmented plain/QoS data. No encryption or HT-control.
    if((fc&0x00ff)!=8&&(fc&0x00ff)!=0x88)return DataError::Layout;
    if((fc&0xc700)||(dataLe16(frame+22)&15))return DataError::Layout;
    const bool qos=(fc&0x80)!=0;
    size_t h=qos?26:24;
    if(qos&&(dataLe16(frame+24)&~uint16_t(7)))return DataError::Layout;
    const uint8_t bssid[6]={0,0x25,0,0xff,0x94,0x73};
    const uint8_t llc[8]={0xaa,0xaa,3,0,0x17,0xf2,8,0};
    // Observed Intel RX form: outer RFC1042 SNAP carrying an exact inner byte count.
    const uint8_t outer[6]={0xaa,0xaa,3,0,0,0};
    if(nativeDiscoveryRx&&qos&&!memcmp(frame+h,outer,6)){
        if(size_t(dataBe16(frame+h+6))!=n-h-8)return DataError::Length;
        h+=8;
    }
    if(memcmp(frame+16,bssid,6)||memcmp(frame+h,llc,8)||
       frame[h+8]!=3||frame[h+9]!=4||frame[h+12]||frame[h+13]||
       dataBe16(frame+h+14)!=0x86dd)return DataError::Protocol;
    const uint8_t *ip=frame+h+16;const size_t ipLength=n-h-16;
    if(!dataIpv6(ip,ipLength))return DataError::Length;
    if(!dataUnicast(local)||!dataUnicast(peer)||memcmp(frame+10,peer,6)||!memcmp(peer,local,6))
        return DataError::Peer;
    if(!dataDestination(frame+4,ip)&&!(nativeDiscoveryRx&&nativeMdnsGroup(frame+4,ip)))return DataError::Address;
    if(!(frame[4]&1)&&memcmp(frame+4,local,6))return DataError::Address;
    const size_t size=ipLength+14;
    if(cap<size)return DataError::Capacity;
    if(dataOverlap(frame,n,out,size)||dataOverlap(peer,6,out,size)||dataOverlap(local,6,out,size))return DataError::Alias;
    memcpy(out,frame+4,6);memcpy(out+6,peer,6);out[12]=0x86;out[13]=0xdd;
    memcpy(out+14,ip,ipLength);awdlSequence=dataLe16(frame+h+10);used=size;
    return DataError::Ok;
}
inline bool dataPlainFrameValid(const uint8_t *p,size_t n){
    if(!p||n<80||n>kAwdlDataMax||p[0]!=8||p[1]||(dataLe16(p+22)&15)||!dataUnicast(p+10))return false;
    const uint8_t bssid[6]={0,0x25,0,0xff,0x94,0x73};
    const uint8_t prefix[10]={0xaa,0xaa,3,0,0x17,0xf2,8,0,3,4};
    return !memcmp(p+16,bssid,6)&&!memcmp(p+24,prefix,sizeof(prefix))&&!p[36]&&!p[37]&&
        dataBe16(p+38)==0x86dd&&dataIpv6(p+40,n-40)&&dataDestination(p+4,p+40);
}
// Restricted raw IBSS sender: plain Data or unaggregated QoS TID0 only.
inline unsigned ibssDataHeaderLength(const uint8_t *p,size_t n){
    if(dataPlainFrameValid(p,n))return 24;
    if(!p||n<82||n>kAwdlDataMax+2||p[0]!=0x88||p[1]||p[24]||p[25])return 0;
    if((dataLe16(p+22)&15)||!dataUnicast(p+10))return 0;
    const uint8_t bssid[]={0,0x25,0,0xff,0x94,0x73},prefix[]={0xaa,0xaa,3,0,0x17,0xf2,8,0,3,4};
    return !memcmp(p+16,bssid,6)&&!memcmp(p+26,prefix,10)&&!p[38]&&!p[39]&&
        dataBe16(p+40)==0x86dd&&dataIpv6(p+42,n-42)&&dataDestination(p+4,p+42)?26:0;
}

}
