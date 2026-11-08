// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Pin the pure decoder's offsets to the actual firmware structures we compile.
static_assert(sizeof(iwm_rx_mpdu_desc)==48,"MQ descriptor ABI");
static_assert(offsetof(iwm_rx_mpdu_desc,status)==12,"MQ status ABI");
static_assert(offsetof(iwm_rx_mpdu_desc,v1.channel)==34,"MQ channel ABI");
static_assert(offsetof(iwm_rx_mpdu_desc,v1.gp2_on_air_rise)==36,"MQ GP2 ABI");
static_assert(offsetof(iwm_rx_mpdu_desc,v1.mac_context)==35,"MQ context ABI");
static_assert(offsetof(iwm_rx_mpdu_desc,v1.filter_match)==24,"MQ filter ABI");
static_assert(offsetof(iwm_rx_mpdu_desc,v1.rate_n_flags)==28,"MQ rate flags ABI");
static_assert(sizeof(iwm_rx_mpdu_res_start)==4,"legacy RX header ABI");
void ItlIwm::awdlRawRxRecord(unsigned stage,const ItlAwdl::RawRxMetadata &m) {
    awdlIbssDataRxStage(stage,m);
    if(awdlNativeDiscovery.open && getMainWorkLoop() && getMainWorkLoop()->inGate()){
        uint64_t rawUptime,rawNowNs;clock_get_uptime(&rawUptime);absolutetime_to_nanoseconds(rawUptime,&rawNowNs);
        awdlNativeDiscovery.observeRaw(stage,m,uint32_t(com.sc_generation),rawNowNs);
    }
    if(!awdlRawRxEnabled || !awdlProbeCounters || stage>=ItlAwdl::RawStageCount)return;
    uint64_t uptime,ns;clock_get_uptime(&uptime);absolutetime_to_nanoseconds(uptime,&ns);
    if(!ItlAwdl::rawRxTraceInLease(awdlTemplateTxEnabled,ItlAwdl::gTemplateRadio.validationArmed,
        ItlAwdl::gEngine.deadlineNs,ns))return;
    awdlProbeCounters->countRawRx(stage,ItlAwdl::rawRxBucket(m.channel));
    const auto &s=awdlRocSession;
    // Keep all cumulative counts. Rows favor social-channel traffic or the
    // command/notification interval, without requiring matching GP2/channel.
    const bool social=m.channel==6 || m.channel==44 || m.channel==149;
    if(!social && s.phase==ItlAwdl::RocPhase::Idle &&
       !awdlRawRxWindow.contains(com.sc_generation,m.channel,m.gp2))return;
    using H=ItlAwdl::RawRxTrace;H::Row row;
    row.v[H::Stage]=stage;row.v[H::Api]=m.api;
    row.v[H::WireLength]=m.wireLength;row.v[H::MPDULength]=m.length;
    row.v[H::DescriptorValid]=m.descriptorValid;row.v[H::LengthValid]=m.lengthValid;
    row.v[H::StatusValid]=m.statusValid;row.v[H::Status]=m.status;
    row.v[H::Channel]=m.channel;row.v[H::RxGp2]=m.gp2;
    row.v[H::MacContext]=m.macContext;row.v[H::MacPhyIndex]=m.macPhyIndex;
    row.v[H::FilterMatch]=m.filterMatch;row.v[H::FrameControl]=m.frameControl;row.v[H::FcValid]=m.fcValid;
    row.v[H::Generation]=uint32_t(com.sc_generation);
    const uint64_t us=ns/1000;row.v[H::HostUsLow]=uint32_t(us);row.v[H::HostUsHigh]=uint32_t(us>>32);
    row.v[H::Attempt]=awdlPeerRocAttempts;row.v[H::Phase]=uint32_t(s.phase);
    row.v[H::TargetChannel]=awdlRocChannel;row.v[H::HomeChannel]=awdlRocHomeChannel;
    row.v[H::Uid]=s.uid;row.v[H::StartGp2]=s.startGp2;row.v[H::EndGp2]=s.endGp2;
    row.v[H::DurationTu]=awdlRocDurationTu;row.v[H::Discovery]=awdlRocIsDiscovery;
    row.v[H::ScanFlags]=com.sc_flags;row.v[H::ScanSubmitting]=awdlScanSubmitting;
    row.v[H::NetState]=com.sc_ic.ic_state;row.v[H::NextNetState]=com.ns_nstate;
    row.v[H::RxControl]=awdlRxControlEnabled;
    awdlProbeCounters->appendRawRx(row);
}
void ItlIwm::awdlRawRxCommon(mbuf_t frame,int channel,uint32_t gp2,uint32_t status) {
    if((!awdlRawRxEnabled && !awdlIbssDataActive()) || !frame)return;
    ItlAwdl::RawRxMetadata m;m.api=3;m.length=uint32_t(mbuf_len(frame));
    m.channel=channel>=0?uint32_t(channel):0;m.gp2=gp2;m.status=status;m.statusValid=true;
    m.lengthValid=true;
    ItlAwdl::rawRxAddresses(m,static_cast<const uint8_t*>(mbuf_data(frame)),mbuf_len(frame));
    if(m.length>=2){m.fcValid=true;m.frameControl=ItlAwdl::rawRx16(static_cast<const uint8_t*>(mbuf_data(frame)));}
    awdlRawRxRecord(ItlAwdl::CommonFrame,m);
}

void ItlIwm::awdlRawRxWire(unsigned stage,const void *packet,size_t wireLength,bool mq) {
    if(!awdlRawRxEnabled && !awdlIbssDataActive())return;
    const auto &phy=com.sc_last_phy_info;
    const auto metadata=ItlAwdl::decodeRawRx(packet,wireLength,mq,
        le16toh(phy.channel),le32toh(phy.system_timestamp));
    awdlRawRxRecord(stage,metadata);
}
