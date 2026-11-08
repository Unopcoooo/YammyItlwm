// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "IbssExperiment.hpp"
namespace ItlIbss {
// Kind58: Length=bytes, Detail0=rate flags, Detail1=airtime us;
// MacContext packs ra_tid/bt_kill/failure_rts/failure_frame, high byte first.
inline bool listenerTxResponseRow(const void *wire,size_t length,unsigned stage,
                                 unsigned queue,unsigned slot,ExperimentRows::Row &row){
 if(!wire||queue!=2||slot>=256||stage<1||stage>3||
    length<sizeof(iwm_tx_resp)+sizeof(iwm_agg_tx_status)+sizeof(uint32_t))return false;
 const auto p=static_cast<const uint8_t*>(wire);
 if(p[offsetof(iwm_tx_resp,frame_count)]!=1)return false;
 auto u16=[&](size_t n)->uint32_t{return uint32_t(p[n])|(uint32_t(p[n+1])<<8);};
 auto u32=[&](size_t n)->uint32_t{return u16(n)|(u16(n+2)<<16);};
 using H=ExperimentRows;H::Row result;
 result.v[H::Kind]=58;result.v[H::Code]=stage;result.v[H::Flags]=(queue<<16)|slot;
 result.v[H::Length]=u16(offsetof(iwm_tx_resp,byte_cnt));
 result.v[H::FrameControl]=u16(offsetof(iwm_tx_resp,frame_ctrl));
 result.v[H::Status]=u16(sizeof(iwm_tx_resp)+offsetof(iwm_agg_tx_status,status));
 result.v[H::Detail0]=u32(offsetof(iwm_tx_resp,initial_rate));
 result.v[H::Detail1]=u16(offsetof(iwm_tx_resp,wireless_media_time));
 result.v[H::MacContext]=(uint32_t(p[offsetof(iwm_tx_resp,ra_tid)])<<24)|
  (uint32_t(p[offsetof(iwm_tx_resp,bt_kill_count)])<<16)|
  (uint32_t(p[offsetof(iwm_tx_resp,failure_rts)])<<8)|p[offsetof(iwm_tx_resp,failure_frame)];
 result.v[H::Valid]=1;row=result;return true;
}
}
