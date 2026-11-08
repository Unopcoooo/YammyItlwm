// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "ResourceLedger.hpp"
// Include after the real native IWM command declarations. Only numeric context
// data is retained; node/BSSID addresses and pointers are never copied out.
namespace ItlP2p {
inline bool decodeResourceWire(uint32_t id,const void *data,size_t length,ResourceCommand &c,
                               uint32_t &detail) {
    c=ResourceCommand();detail=0;
    if(!data || (id!=0x8 && id!=0x28 && id!=0x2b))return false;
    const auto p=static_cast<const uint8_t*>(data);
    auto word=[&](size_t at) {return uint32_t(p[at])|(uint32_t(p[at+1])<<8)|
        (uint32_t(p[at+2])<<16)|(uint32_t(p[at+3])<<24);};
    if(length<8)return false;
    c.context=word(0);c.action=word(4);
    if(id==0x8) {
        static_assert(sizeof(iwm_phy_context_cmd)==36 && sizeof(iwm_phy_context_cmd_uhb)==40,"PHY ABI");
        static_assert(offsetof(iwm_phy_context_cmd,ci)==16 && offsetof(iwm_phy_context_cmd_uhb,ci)==16,"PHY channel offset");
        c.kind=ResourceKind::Phy;
        if(length==sizeof(iwm_phy_context_cmd)) {
            c.channel=p[17];detail=uint32_t(p[16])|(uint32_t(p[18])<<8)|(uint32_t(p[19])<<16);
        } else if(length==sizeof(iwm_phy_context_cmd_uhb)) {
            c.channel=word(16);detail=uint32_t(p[20])|(uint32_t(p[21])<<8)|(uint32_t(p[22])<<16);
        } else return false;
    } else if(id==0x28) {
        static_assert(offsetof(iwm_mac_ctx_cmd,filter_flags)==52 && offsetof(iwm_mac_ctx_cmd,tsf_id)==12,"MAC offsets");
        c.kind=ResourceKind::Mac;
        if(length!=sizeof(iwm_mac_ctx_cmd))return false;
        c.role=word(8);c.tsf=word(12);detail=word(52);
    } else {
        static_assert(sizeof(iwm_binding_cmd_v1)==24 && sizeof(iwm_binding_cmd)==28,"binding ABI");
        static_assert(offsetof(iwm_binding_cmd,macs)==8 && offsetof(iwm_binding_cmd,phy)==20,"binding offsets");
        c.kind=ResourceKind::Binding;
        if(length!=sizeof(iwm_binding_cmd_v1) && length!=sizeof(iwm_binding_cmd))return false;
        for(unsigned i=0;i<3;++i)c.macs[i]=word(8+4*i);
        c.phy=word(20);c.lmac=length==sizeof(iwm_binding_cmd)?word(24):0;
    }
    return true; // Wire shape only; malformed/unsupported IDs/actions remain diagnostics.
}
}
