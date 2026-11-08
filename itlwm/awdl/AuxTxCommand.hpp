// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "DataFrames.hpp"
// Included after the real IWM packed declarations (or matching host shims).
namespace ItlAwdl {
// Legacy API v6 describes life_time as "usecs??". Keep the existing management
// value; the data value below is an experimental field value, not a timer proof.
constexpr uint32_t kAuxManagementLifetimeValue=2000,kAuxDataLifetimeValue=4000;
inline bool auxPlainDataValid(const uint8_t *p,size_t n){return dataPlainFrameValid(p,n);}
template<class Command> bool configureAuxTxCommand(const uint8_t *frame,size_t n,uint8_t antenna,
                                                   unsigned priority,uint64_t scratch,unsigned index,Command &command){
    static_assert(sizeof(iwm_tx_cmd)==56&&sizeof(iwm_cmd_header)==4,"Legacy AUX TX ABI");
    if(!frame||n<40||(antenna!=1&&antenna!=2&&antenna!=4)||priority>3||index>=256||
       !scratch||scratch>=(1ULL<<36)||(scratch&3)||sizeof(command.data)<sizeof(iwm_tx_cmd)+24)return false;
    const bool data=frame[0]==8;
    const uint8_t broadcast[6]={255,255,255,255,255,255};
    if(data){if(!auxPlainDataValid(frame,n))return false;}
    else if(n>1536||frame[0]!=0xd0||frame[1]||memcmp(frame+4,broadcast,6)||!dataUnicast(frame+10)||
            (dataLe16(frame+22)&15)||frame[24]!=127||frame[25]||frame[26]!=0x17||frame[27]!=0xf2||frame[28]!=8)return false;
    memset(&command,0,sizeof(command));command.hdr.code=IWM_TX_CMD;
    command.hdr.qid=IWM_DQA_AUX_QUEUE;command.hdr.idx=uint8_t(index);
    auto tx=reinterpret_cast<iwm_tx_cmd *>(command.data);
    tx->len=htole16(uint16_t(n));tx->sta_id=IWM_AUX_STA_ID;
    tx->tid_tspec=data?IWM_TID_NON_QOS:IWM_MAX_TID_COUNT;
    tx->life_time=htole32(data?kAuxDataLifetimeValue:kAuxManagementLifetimeValue);
    tx->rate_n_flags=htole32(IWL_RATE_6M_PLCP|(uint32_t(antenna)<<RATE_MCS_ANT_POS));
    // Firmware owns MAC sequence allocation for management and non-QoS data.
    // ACK is meaningful only for a unicast receiver; DIRECT_DONE alone is not ACK.
    const bool ack=data&&!(frame[4]&1);
    tx->tx_flags=htole32(IWM_TX_CMD_FLG_SEQ_CTL|(ack?IWM_TX_CMD_FLG_ACK:0)|
                        (priority<<IWM_TX_CMD_FLG_BT_PRIO_POS));
    if(data){tx->data_retry_limit=1;tx->rts_retry_limit=1;}
    tx->dram_lsb_ptr=htole32(uint32_t(scratch));tx->dram_msb_ptr=uint8_t(scratch>>32);
    memcpy(command.data+sizeof(*tx),frame,24);return true;
}
}
