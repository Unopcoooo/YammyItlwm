// Actual production tracing with deterministic transport/clock shims; no radio.
#include "../../itlwm/awdl/RawRxMetadata.hpp"
#include "../../itlwm/awdl/RawRxTrace.hpp"
#include "../../itlwm/awdl/RxControl.hpp"
#include "../../itlwm/awdl/AuxRoc.hpp"
#include "../../itlwm/awdl/TemplateRuntime.hpp"
#include "../../itlwm/awdl/EngineState.hpp"
#include "../../itlwm/awdl/NativeDiscoverySession.hpp"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <memory>
#include <vector>
namespace ItlAwdl {EngineState gEngine={};TemplateRadioState gTemplateRadio={};}
struct iwm_rx_mpdu_desc {
 uint8_t head[12];uint16_t status;uint8_t middle[10];
 struct {uint32_t filter_match,rate_n_flags;uint16_t reserved;uint8_t channel,mac_context;uint32_t gp2_on_air_rise;uint64_t tsf;} v1;
};
struct iwm_rx_mpdu_res_start {uint8_t bytes[4];};
static uint64_t now=10000000000ULL;
static void clock_get_uptime(uint64_t *out){*out=now;}
static void absolutetime_to_nanoseconds(uint64_t in,uint64_t *out){*out=in;}
static uint16_t le16toh(uint16_t x){return x;}static uint32_t le32toh(uint32_t x){return x;}
struct Mbuf {uint8_t bytes[24]={};};using mbuf_t=Mbuf*;
static size_t mbuf_len(mbuf_t){return 24;}static void *mbuf_data(mbuf_t m){return m->bytes;}
struct Counters {
 uint64_t counts[ItlAwdl::RawStageCount][5]={};std::vector<ItlAwdl::RawRxTrace::Row> rows;
 void countRawRx(unsigned stage,unsigned bucket){++counts[stage][bucket];}
 void appendRawRx(const ItlAwdl::RawRxTrace::Row &r){rows.push_back(r);}
};
struct ItlIwm {
 ItlAwdl::NativeDiscoverySession awdlNativeDiscovery;
 struct Loop {bool gated=true;bool inGate(){return gated;}} loop;
 Loop *getMainWorkLoop(){return &loop;}
 bool ibssActive=false;unsigned ibssTapCalls=0;
 bool awdlIbssDataActive(){return ibssActive;}
 void awdlIbssDataRxStage(unsigned,const ItlAwdl::RawRxMetadata &){if(ibssActive)++ibssTapCalls;}
 bool awdlRawRxEnabled=true,awdlTemplateTxEnabled=true,awdlRocIsDiscovery=false,awdlRxControlEnabled=false;
 Counters counters,*awdlProbeCounters=&counters;ItlAwdl::AuxRocSession awdlRocSession;
 ItlAwdl::ReceiveWindow awdlRawRxWindow;
 unsigned awdlPeerRocAttempts=0,awdlRocChannel=6,awdlRocHomeChannel=165,awdlRocDurationTu=58,awdlScanSubmitting=0;
 struct {unsigned sc_generation=7,sc_flags=0,ns_nstate=1;struct {unsigned ic_state=1;}sc_ic;
 struct {uint16_t channel=165;uint32_t system_timestamp=1;}sc_last_phy_info;}com;
 void awdlRawRxRecord(unsigned,const ItlAwdl::RawRxMetadata &);
 void awdlRawRxCommon(mbuf_t,int,uint32_t,uint32_t);
 void awdlRawRxWire(unsigned,const void *,size_t,bool);
};
#include "../../itlwm/awdl/RawRxRuntime.hpp"
int main(){
 using namespace ItlAwdl;using H=RawRxTrace;
 for(unsigned flags=0;flags<8;++flags)assert(rawRxTraceRequested(flags&1,flags&2,flags&4)==bool((flags&4)&&(flags&3)));
 uint8_t wire[72]={};wire[0]=24;wire[7]=0x24;wire[12]=3;wire[24]=0x78;wire[34]=6;wire[35]=4;
 wire[36]=0x44;wire[37]=0x33;wire[38]=0x22;wire[39]=0x11;wire[40]=0xbb;wire[48]=0xd0;
 auto h=std::make_unique<ItlIwm>();
 h->awdlRawRxWire(RawIngress,wire,sizeof(wire),true);assert(h->counters.rows.empty()&&!h->counters.counts[RawIngress][1]);
 gTemplateRadio.validationArmed=true;h->awdlRawRxWire(RawIngress,wire,sizeof(wire),true);assert(h->counters.rows.empty());
 gEngine.deadlineNs=now+300000000000ULL;
 h->awdlRawRxWire(RawIngress,wire,sizeof(wire),true);
 const auto &r=h->counters.rows.back();assert(r.v[H::RxGp2]==0x11223344&&r.v[H::MacContext]==4&&r.v[H::MacPhyIndex]==0x24&&r.v[H::FilterMatch]==0x78);
 assert(r.v[H::Channel]==6&&r.v[H::FrameControl]==0xd0&&r.v[H::DescriptorValid]&&r.v[H::FcValid]);
 const auto m=decodeRawRx(wire,sizeof(wire),true);h->awdlRawRxRecord(RejectDecrypt,m);
 assert(h->counters.counts[RawIngress][1]==1&&h->counters.counts[RejectDecrypt][1]==1&&!h->counters.counts[CommonFrame][1]);
 Mbuf packet;packet.bytes[0]=0xd0;h->awdlRawRxCommon(&packet,6,0x11223344,3);
 assert(h->counters.counts[CommonFrame][1]==1&&h->counters.rows.back().v[H::Api]==3&&!h->counters.rows.back().v[H::DescriptorValid]);
 // Zero MAC context in the common callback is unavailable metadata, not proof of MAC0.
 now=uint64_t(gEngine.deadlineNs)+2000000000ULL;h->awdlRawRxWire(RawIngress,wire,sizeof(wire),true);
 const auto count=h->counters.rows.size();++now;h->awdlRawRxWire(RawIngress,wire,sizeof(wire),true);assert(h->counters.rows.size()==count);
 h->awdlRawRxEnabled=false;h->awdlRawRxWire(RawIngress,wire,sizeof(wire),true);assert(h->counters.rows.size()==count);
 h->awdlRawRxEnabled=true;h->awdlTemplateTxEnabled=false;gEngine.deadlineNs=0;gTemplateRadio.validationArmed=false;
 h->awdlRawRxWire(RawIngress,wire,sizeof(wire),true);assert(h->counters.rows.size()==count+1);
 h->awdlRawRxEnabled=false;h->ibssActive=true;wire[29]=1;
 h->awdlRawRxWire(RawIngress,wire,sizeof(wire),true);assert(h->ibssTapCalls==1&&h->counters.rows.size()==count+1);
 auto ht=decodeRawRx(wire,sizeof(wire),true);assert(ht.rateValid&&ht.rateFlags==256);
 // Exercise the actual raw callback and the session counter across every drop stage.
 auto &s=h->awdlNativeDiscovery;uint8_t local[]={2,3,4,5,6,7};
 assert(s.begin(1,2,7,local,now,now+1000000000));s.open=true;
 wire[48]=0x88;wire[49]=0;memcpy(wire+52,local,6);
 h->awdlRawRxWire(RawIngress,wire,sizeof(wire),true);
 assert(s.rxTarget[RawIngress]==1&&s.rxTargetPlain[RawIngress]==1);
 auto localMeta=decodeRawRx(wire,sizeof(wire),true);
 for(unsigned stage=1;stage<RawStageCount;++stage)h->awdlRawRxRecord(stage,localMeta);
 for(unsigned stage=0;stage<RawStageCount;++stage)assert(s.rxTarget[stage]==1&&s.rxTargetPlain[stage]==1);
 assert((s.rxTargetFirst[0]&0xffff)==0x88&&((s.rxTargetFirst[0]>>16)&0xffff)==24);
 auto bad=localMeta;bad.receiver[0]^=2;h->awdlRawRxRecord(RawIngress,bad);
 bad=localMeta;bad.addressValid=false;h->awdlRawRxRecord(RawIngress,bad);
 bad=localMeta;bad.lengthValid=false;h->awdlRawRxRecord(RawIngress,bad);
 bad=localMeta;bad.channel=44;h->awdlRawRxRecord(RawIngress,bad);
 ++h->com.sc_generation;h->awdlRawRxRecord(RawIngress,localMeta);--h->com.sc_generation;
 h->loop.gated=false;h->awdlRawRxRecord(RawIngress,localMeta);h->loop.gated=true;
 h->awdlRawRxRecord(RawStageCount,localMeta);
 assert(s.rxTarget[0]==1);
 bad=localMeta;bad.frameControl|=0x4000;h->awdlRawRxRecord(RawIngress,bad);
 assert(s.rxTarget[0]==2&&s.rxTargetPlain[0]==1);
 s.rxTarget[0]=s.rxTargetPlain[0]=UINT32_MAX;h->awdlRawRxRecord(RawIngress,localMeta);
 assert(s.rxTarget[0]==UINT32_MAX&&s.rxTargetPlain[0]==UINT32_MAX);
 now=s.endNs;h->awdlRawRxRecord(CommonFrame,localMeta);assert(s.rxTarget[CommonFrame]==1);
 --now;s.close();h->awdlRawRxRecord(CommonFrame,localMeta);assert(s.rxTarget[CommonFrame]==1);
 s.controlRetired=true;assert(s.canRearm(2));s.resetAfterRetirement();
 assert(s.begin(1,3,7,local,now,now+1000)&&!s.rxTarget[0]&&!s.rxTargetFirst[0]);
 puts("PASS actual raw RX runtime: explicit discovery/template opt-in, template boot and absent lease stay silent, descriptor context/filter/GP2 distinct from TSF, per-stage drops vs common callbacks, finite two-second tail, disabled and legacy policy; no frame bodies/addresses or hardware writes.");
}
