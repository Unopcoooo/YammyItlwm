// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "Descriptor.hpp"
#include "MdnsMulticast.hpp"
namespace ItlIbss {
inline bool listenerReservation(const Reservation &r){
 return r.valid()&&r.mac==1&&r.phy==1&&r.tsf==1&&r.mcastSta==3&&r.bcastSta==4;
}
// Shared construction keeps the monitor and CAB paths on the same DMA checks.
inline bool listenerOrMdnsDescriptor(bool mdns,const Reservation &r,const uint8_t *frame,size_t length,
 uint64_t commandAddress,uint64_t scratchAddress,const Segment *segments,unsigned count,
 unsigned slot,uint8_t antenna,unsigned priority,unsigned lifetime,iwm_device_cmd &command,iwm_tfd &tfd,uint16_t &scheduler){
 if(!listenerReservation(r)||!frame||length<40||length>2304||(antenna!=1&&antenna!=2&&antenna!=4)||priority>3)return false;
 const uint8_t bssid[]={0,0x25,0,0xff,0x94,0x73};
 if(mdns&&(!lifetime||lifetime>2000||!ItlAwdl::mdnsMulticastFrame(frame,length)))return false;
 const bool action=frame[0]==0xd0&&!frame[1]&&length>=40&&frame[24]==127&&
  !memcmp(frame+25,"\x00\x17\xf2\x08\x10",5)&&(frame[30]==0||frame[30]==3);
 if(!ItlAwdl::dataUnicast(frame+10)||memcmp(frame+16,bssid,6)||
    (!action&&!ItlAwdl::dataPlainFrameValid(frame,length))||
    !descriptorBuffersValid(length,commandAddress,scratchAddress,segments,count,slot,sizeof(command.data)))return false;
 if(action){for(unsigned i=4;i<10;++i)if(frame[i]!=255)return false;}
 if(ItlAwdl::dataOverlap(frame,length,reinterpret_cast<const uint8_t*>(&command),sizeof(command))||
    ItlAwdl::dataOverlap(frame,length,reinterpret_cast<const uint8_t*>(&tfd),sizeof(tfd))||
    ItlAwdl::dataOverlap(reinterpret_cast<const uint8_t*>(segments),count*sizeof(Segment),reinterpret_cast<const uint8_t*>(&command),sizeof(command))||
    ItlAwdl::dataOverlap(reinterpret_cast<const uint8_t*>(segments),count*sizeof(Segment),reinterpret_cast<const uint8_t*>(&tfd),sizeof(tfd)))return false;
 if(dmaOverlap(commandAddress,sizeof(command),scratchAddress,4)&&scratchAddress!=commandAddress+sizeof(iwm_cmd_header)+offsetof(iwm_tx_cmd,scratch))return false;
 memset(&command,0,sizeof(command));memset(&tfd,0,sizeof(tfd));
 command.hdr.code=IWM_TX_CMD;command.hdr.qid=mdns?r.mcastQueue:2;command.hdr.idx=uint8_t(slot);
 auto tx=reinterpret_cast<iwm_tx_cmd*>(command.data);txLe16(&tx->len,uint16_t(length));tx->sta_id=r.mcastSta;tx->tid_tspec=action?8:0;
 // NO_ACK avoids retries outside the peer's visit; this is not hardware ACK evidence.
 txLe32(&tx->tx_flags,IWM_TX_CMD_FLG_SEQ_CTL|(priority<<IWM_TX_CMD_FLG_BT_PRIO_POS));
 txLe32(&tx->rate_n_flags,0x0d|(uint32_t(antenna)<<14));txLe32(&tx->life_time,mdns?lifetime:2000);
 tx->data_retry_limit=tx->rts_retry_limit=1;txLe32(&tx->dram_lsb_ptr,uint32_t(scratchAddress));tx->dram_msb_ptr=uint8_t(scratchAddress>>32);
 memcpy(command.data+sizeof(*tx),frame,24);
 const unsigned tail=(sizeof(iwm_cmd_header)+sizeof(*tx)+24-20+3)&~3u;
 tfd.num_tbs=uint8_t(2+count);transferBuffer(tfd.tbs[0],commandAddress,20);transferBuffer(tfd.tbs[1],commandAddress+20,tail);
 for(unsigned i=0;i<count;++i)transferBuffer(tfd.tbs[i+2],segments[i].address,segments[i].length);
 scheduler=ItlAwdl::auxUnencryptedSchedulerEntry(uint16_t(length),r.mcastSta);return true;
}
inline bool listenerDescriptor(const Reservation &r,const uint8_t *frame,size_t length,
 uint64_t commandAddress,uint64_t scratchAddress,const Segment *segments,unsigned count,
 unsigned slot,uint8_t antenna,unsigned priority,unsigned lifetime,iwm_device_cmd &command,iwm_tfd &tfd,uint16_t &scheduler){
 return listenerOrMdnsDescriptor(false,r,frame,length,commandAddress,scratchAddress,segments,count,slot,antenna,priority,lifetime,command,tfd,scheduler);
}
// IBSS data uses the multicast station and CAB queue; management stays on bcastSta.
inline bool mdnsDescriptor(const Reservation &r,const uint8_t *frame,size_t length,
 uint64_t commandAddress,uint64_t scratchAddress,const Segment *segments,unsigned count,
 unsigned slot,uint8_t antenna,unsigned priority,unsigned lifetime,iwm_device_cmd &command,iwm_tfd &tfd,uint16_t &scheduler){
 return listenerOrMdnsDescriptor(true,r,frame,length,commandAddress,scratchAddress,segments,count,slot,antenna,priority,lifetime,command,tfd,scheduler);
}

}
