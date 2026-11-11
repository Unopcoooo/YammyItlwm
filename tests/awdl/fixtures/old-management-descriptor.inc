namespace ItlIbss {
inline bool descriptorBefore(const Reservation &r,const uint8_t *frame,size_t length,
                       uint64_t commandAddress,uint64_t scratchAddress,
                       const Segment *segments,unsigned count,unsigned slot,
                       uint8_t antenna,unsigned coexistPriority,unsigned lifetimeUs,
                       iwm_device_cmd &command,iwm_tfd &tfd,uint16_t &scheduler) {
    if(!frame || length<40 || length>2304 || frame[0]!=0xd0 || frame[1] ||
       !(frame[4]&1) || (frame[10]&1) || (frame[22]&15) ||
       frame[24]!=127 || frame[25]!=0 || frame[26]!=0x17 || frame[27]!=0xf2 || frame[28]!=8 ||
       !segments || !count || count>IWM_NUM_OF_TBS-2 || slot>=256 ||
       (antenna!=1 && antenna!=2 && antenna!=4) || coexistPriority>3 ||
       !lifetimeUs || lifetimeUs>2000)return false;
    uint8_t source=0;for(unsigned i=10;i<16;++i)source|=frame[i];if(!source)return false;
    ManagementRoute route;if(!managementRoute(r,0xd0,true,route))return false;
    const unsigned commandBytes=sizeof(iwm_cmd_header)+sizeof(iwm_tx_cmd)+24;
    const unsigned tail=(commandBytes-20+3)&~3u;
    if(sizeof(iwm_tx_cmd)+24>sizeof(command.data) || (commandAddress&3) || (scratchAddress&3) ||
       !dmaSpan(commandAddress,20+tail) || !dmaSpan(scratchAddress,4) ||
       scratchAddress!=commandAddress+sizeof(iwm_cmd_header)+offsetof(iwm_tx_cmd,scratch))return false;
    uint64_t total=0;
    for(unsigned i=0;i<count;++i){
        const auto &s=segments[i];
        if(s.length>4095 || !dmaSpan(s.address,s.length) ||
           dmaOverlap(s.address,s.length,commandAddress,20+tail) || dmaOverlap(s.address,s.length,scratchAddress,4))return false;
        for(unsigned j=0;j<i;++j)if(dmaOverlap(s.address,s.length,segments[j].address,segments[j].length))return false;
        total+=s.length;
    }
    if(total!=length-24)return false;
    // Commit outputs only after every shape/range check succeeds.
    memset(&command,0,sizeof(command));memset(&tfd,0,sizeof(tfd));
    command.hdr.code=IWM_TX_CMD;command.hdr.qid=route.queue;command.hdr.idx=uint8_t(slot);
    auto tx=reinterpret_cast<iwm_tx_cmd*>(command.data);
    txLe16(&tx->len,uint16_t(length));tx->sta_id=route.station;tx->tid_tspec=8;
    txLe32(&tx->life_time,lifetimeUs);txLe32(&tx->rate_n_flags,0x0d|(uint32_t(antenna)<<14)); // 6Mbps OFDM
    txLe32(&tx->tx_flags,IWM_TX_CMD_FLG_SEQ_CTL|(coexistPriority<<IWM_TX_CMD_FLG_BT_PRIO_POS));
    txLe32(&tx->dram_lsb_ptr,uint32_t(scratchAddress));tx->dram_msb_ptr=uint8_t(scratchAddress>>32);
    memcpy(command.data+sizeof(*tx),frame,24);
    tfd.num_tbs=uint8_t(2+count);transferBuffer(tfd.tbs[0],commandAddress,20);transferBuffer(tfd.tbs[1],commandAddress+20,tail);
    for(unsigned i=0;i<count;++i)transferBuffer(tfd.tbs[i+2],segments[i].address,segments[i].length);
    scheduler=ItlAwdl::auxUnencryptedSchedulerEntry(uint16_t(length),route.station);
    return true;
}
}
