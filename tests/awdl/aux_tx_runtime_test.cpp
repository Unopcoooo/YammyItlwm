// Compiles the real AuxTxRuntime.hpp against deterministic mbuf/DMA/firmware
// shims. Tests adapter ownership and descriptors, not physical radio behaviour.
#define AIRPORT_VTD 1
#include "../../itlwm/awdl/AuxRoc.hpp"
#include "../../itlwm/awdl/AuxTx.hpp"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <memory>
#define isset(p,b) (((p)[(b)/8] & (1u<<((b)%8)))!=0)
#define _ALIGN(n,a) (((n)+(a)-1)&~((a)-1))
#define IWM_WRITE(sc,reg,v) ((void)(reg),(sc)->doorbell=(v))
constexpr unsigned IWM_FLAG_SHUTDOWN=1,IWM_FLAG_HW_ERR=2,IWM_FLAG_RFKILL=4,IWM_FLAG_SCANNING=8,IWM_FLAG_BGSCAN=16;
constexpr unsigned IEEE80211_S_RUN=1,IWM_DQA_AUX_QUEUE=1,IWM_AUX_STA_ID=1;
constexpr unsigned IWM_UCODE_TLV_CAPA_DQA_SUPPORT=31,IWM_DEVICE_SYSTEM_TIME_REG=0xa0206c;
constexpr unsigned MBUF_DONTWAIT=0,IWM_NUM_OF_TBS=20,IWM_MAX_TID_COUNT=16,IWM_TID_NON_QOS=0,IWL_RATE_6M_PLCP=13,RATE_MCS_ANT_POS=14;
constexpr unsigned IWM_TX_CMD_FLG_ACK=1<<3,IWM_TX_CMD_FLG_SEQ_CTL=1<<13,IWM_TX_CMD_FLG_BT_PRIO_POS=11,EDCA_AC_BE=0;
constexpr unsigned IWM_TX_CMD=0x1c,IWM_TX_RING_COUNT=256,IWM_TFD_QUEUE_SIZE_BC_DUP=64,IWM_TFD_QUEUE_SIZE_MAX=256;
constexpr unsigned IWM_TX_STATUS_MSK=255,IWM_TX_STATUS_SUCCESS=1,IWM_TX_STATUS_DIRECT_DONE=2;
constexpr unsigned IWM_HBUS_TARG_WRPTR=0;
struct FakeMbuf { uint8_t bytes[64]={}; unsigned offset=0,len=0,packetlen=0; };
using mbuf_t=FakeMbuf*;
static unsigned outstanding=0; static bool failAlloc=false,failMap=false;
static int mbuf_allocpacket(unsigned,size_t,unsigned*,mbuf_t *out) {
    if(failAlloc) { *out=nullptr; return 12; } *out=new FakeMbuf; ++outstanding; return 0;
}
static mbuf_t mbuf_next(mbuf_t) { return nullptr; }
static unsigned mbuf_maxlen(mbuf_t) { return 64; }
static void mbuf_setlen(mbuf_t m,unsigned n) { m->len=n; }
static void mbuf_pkthdr_setlen(mbuf_t m,unsigned n) { m->packetlen=n; }
static void *mbuf_data(mbuf_t m) { return m->bytes+m->offset; }
static void mbuf_adj(mbuf_t m,unsigned n) { assert(n<=m->len);m->offset+=n;m->len-=n;m->packetlen-=n; }
static void mbuf_freem(mbuf_t m) { assert(outstanding);--outstanding;delete m; }
struct IOPhysicalSegment { uint64_t location=0; size_t length=0; };
struct FakeMap { uint8_t bytes[64]={}; };
static unsigned bus_dmamap_packet(FakeMap *map,mbuf_t m,IOPhysicalSegment *seg,unsigned,bool) {
    if(failMap)return 0;
    assert(m->len==16 && m->packetlen==16);memcpy(map->bytes,mbuf_data(m),16);
    seg[0].location=0x100000;seg[0].length=16;return 1;
}
struct ieee80211_frame { uint8_t bytes[24]; };
struct iwm_cmd_header { uint8_t code=0,flags=0,qid=0,idx=0; };
struct __attribute__((packed)) iwm_tx_cmd {
    uint16_t len,offload_assist; uint32_t tx_flags; uint8_t scratch[4]; uint32_t rate_n_flags;
    uint8_t sta_id,sec_ctl,initial_rate_index,reserved2,key[16]; uint16_t next_frame_flags,reserved3;
    uint32_t life_time,dram_lsb_ptr; uint8_t dram_msb_ptr,rts_retry_limit,data_retry_limit,tid_tspec;
    uint16_t pm_frame_timeout,driver_txop;
};
static_assert(sizeof(iwm_tx_cmd)==56,"legacy TX wire size");
struct FakeCommand { iwm_cmd_header hdr; uint8_t data[128]={}; };
struct FakeDesc { unsigned num_tbs=0; struct { uint32_t lo=0; uint16_t hi_n_len=0; } tbs[20]; };
struct FakeData {
    FakeMap bounce; FakeMap *map=&bounce; uint64_t scratch_paddr=0x100000,cmd_paddr=0x100000;
    mbuf_t m=nullptr; void *in=nullptr; int totlen=0,txmcs=0,txrate=0; uint16_t fc=0; uint8_t info[64]={};
};
struct FakeRing {
    unsigned cur=0,tail=0,queued=0; FakeDesc descriptors[256]; FakeCommand commands[256];
    FakeDesc *desc=descriptors; FakeCommand *cmd=commands; FakeData data[256];
};
struct iwm_agn_scd_bc_tbl { uint16_t tfd_offset[512]={}; };
struct FakeIc { unsigned ic_state=1; uint8_t ic_myaddr[6]={2,1,2,3,4,5}; };
struct FakeSc {
    FakeIc sc_ic; int sc_generation=1; unsigned ns_nstate=1,sc_flags=0,doorbell=0;
    uint8_t sc_enabled_capa[8]={}; FakeRing txq[16]; iwm_agn_scd_bc_tbl tables[16];
    struct { void *vaddr; } sched_dma{tables};
};
static uint8_t iwm_fw_valid_tx_ant(FakeSc*) { return 3; }
static uint8_t iwm_get_dma_hi_addr(uint64_t p) { return uint8_t(p>>32); }
static uint16_t htole16(uint16_t n) { return n; }
static uint32_t htole32(uint32_t n) { return n; }
static uint64_t nowNs=10000000000ULL;
static uint64_t itlAwdlRocNowNs() { return nowNs; }
struct FakeLoop { bool inGate() const {return true;} };
struct ItlAwdlProbeCounters {
    enum Counter { AuxTxAttempts,AuxTxSkipped,AuxTxResourceErrors,AuxTxSubmitted,AuxTxCompleted,AuxTxSuccess,
        AuxTxFailed,AuxTxCleanupTimeouts,AuxTxFlushes,AuxTxFlushErrors,Count };
    enum Field { TxPhase,TxStatus,TxQueue,TxIndex,TxSubmittedGp2,FieldCount };
    unsigned counts[Count]={},fields[FieldCount]={};
    void add(Counter c) {++counts[c];} void setRoc(Field f,uint32_t v) {fields[f]=v;}
};
class ItlIwm {
public:
    FakeSc com; FakeLoop loop; ItlAwdlProbeCounters counters; ItlAwdlProbeCounters *awdlProbeCounters=&counters;
    unsigned awdlScanSubmitting=0;
    bool awdlAuxTxEnabled=true,dmaDetaching=false; ItlAwdl::AuxTxOwnership awdlAuxTx;
    ItlAwdl::AuxRocSession awdlRocSession;
    unsigned reads=0,flushes=0,resets=0,frees=0; uint32_t secondGp2=100100;
    bool flushError=false,resetDuringFlush=false,completeDuringFlush=false;
    ItlIwm() {
        com.sc_enabled_capa[31/8]|=1u<<(31%8);
        awdlRocSession.generation=1;awdlRocSession.phase=ItlAwdl::RocPhase::Running;awdlRocSession.startGp2=100000;
    }
    FakeLoop *getMainWorkLoop() {return &loop;}
    bool iwm_nic_lock(FakeSc*) {return true;} void iwm_nic_unlock(FakeSc*) {}
    uint32_t iwm_read_prph(FakeSc*,unsigned) {return ++reads==1?100000:secondGp2;}
    unsigned iwm_coex_tx_prio(FakeSc*,ieee80211_frame*,unsigned) {return 0;}
    void iwm_reset_sched(FakeSc*,unsigned q,unsigned idx,unsigned station) {
        ++resets;assert(station==1);com.tables[q].tfd_offset[idx]=0x1001;
    }
    void iwm_txd_done(FakeSc*,FakeData *d) {++frees;assert(!d->in);mbuf_freem(d->m);d->m=nullptr;}
    void awdlRocPublish() {}
    int iwm_flush_tx_path(FakeSc*,unsigned mask) {
        ++flushes;assert(mask==2);
        if(resetDuringFlush) {++com.sc_generation;auto &d=com.txq[1].data[0];iwm_txd_done(&com,&d);com.txq[1].queued=0;awdlAuxTx.deviceQuiesced();}
        if(completeDuringFlush) awdlAuxTxComplete(1,0,0x85);
        return flushError?5:0;
    }
    void awdlAuxTxPublish();bool awdlAuxTxDrain();void awdlAuxTxStart();
    bool awdlAuxTxComplete(unsigned,unsigned,uint32_t);void awdlAuxTxTick();
};
#include "../../itlwm/awdl/AuxTxRuntime.hpp"
int main() {
    using C=ItlAwdlProbeCounters;using P=ItlAwdl::AuxTxPhase;
    auto a=std::make_unique<ItlIwm>();a->awdlAuxTxStart();
    assert(a->com.doorbell==0x101 && a->com.txq[1].queued==1 && outstanding==1);
    auto tx=reinterpret_cast<iwm_tx_cmd*>(a->com.txq[1].cmd[0].data);
    assert(tx->len==40 && tx->sta_id==1 && tx->sec_ctl==0 && tx->life_time==2000);
    assert(tx->rate_n_flags==(13|(1<<14)) && tx->tx_flags==(1<<13));
    assert(a->com.tables[1].tfd_offset[0]==0x100c && a->com.txq[1].desc[0].num_tbs==3);
    assert(!a->awdlAuxTxComplete(0,0,1) && !a->awdlAuxTxComplete(1,1,1));
    a->awdlRocSession.phase=ItlAwdl::RocPhase::Draining;
    assert(a->awdlAuxTxComplete(1,0,1));
    assert(a->frees==1 && !outstanding && a->com.txq[1].queued==0 && a->counters.counts[C::AuxTxSuccess]==1);
    assert(a->awdlRocSession.phase==ItlAwdl::RocPhase::Idle && a->awdlAuxTxDrain());
    a->awdlAuxTxStart();assert(a->counters.counts[C::AuxTxAttempts]==1);
    auto late=std::make_unique<ItlIwm>();late->secondGp2=102001;late->awdlAuxTxStart();
    assert(!outstanding && !late->com.doorbell && late->awdlAuxTx.phase==P::Skipped);
    auto flushed=std::make_unique<ItlIwm>();flushed->awdlAuxTxStart();
    flushed->awdlRocSession.phase=ItlAwdl::RocPhase::Draining;flushed->awdlAuxTxTick();
    assert(flushed->flushes==1 && outstanding==1 && !flushed->awdlAuxTxDrain());
    nowNs+=3000000000ULL;flushed->awdlAuxTxTick();flushed->awdlAuxTxTick();
    assert(flushed->flushes==1 && outstanding==1 && flushed->awdlAuxTx.phase==P::Quarantined);
    assert(flushed->awdlAuxTxComplete(1,0,0x85) && !outstanding);
    auto reset=std::make_unique<ItlIwm>();reset->resetDuringFlush=true;reset->awdlAuxTxStart();
    reset->awdlRocSession.phase=ItlAwdl::RocPhase::Draining;reset->awdlAuxTxTick();
    assert(!outstanding && !reset->awdlAuxTx.pending && reset->awdlAuxTx.phase==P::Stopped);
    auto done=std::make_unique<ItlIwm>();done->completeDuringFlush=true;done->awdlAuxTxStart();
    done->awdlRocSession.phase=ItlAwdl::RocPhase::Draining;done->awdlAuxTxTick();
    assert(!outstanding && done->awdlAuxTx.phase==P::Completed);
    failMap=true;auto map=std::make_unique<ItlIwm>();map->awdlAuxTxStart();failMap=false;
    assert(!outstanding && !map->com.doorbell && map->counters.counts[C::AuxTxResourceErrors]==1);
    auto legacy=std::make_unique<ItlIwm>();memset(legacy->com.sc_enabled_capa,0,sizeof(legacy->com.sc_enabled_capa));legacy->awdlAuxTxStart();
    assert(!outstanding && legacy->awdlAuxTx.attempted && !legacy->awdlAuxTx.pending);
    auto scanning=std::make_unique<ItlIwm>();scanning->awdlScanSubmitting=1;scanning->awdlAuxTxStart();
    assert(!outstanding && scanning->awdlAuxTx.attempted && !scanning->awdlAuxTx.pending);
    auto disabled=std::make_unique<ItlIwm>();disabled->awdlAuxTxEnabled=false;disabled->awdlAuxTxStart();
    assert(!outstanding && !disabled->awdlAuxTx.attempted);
    puts("actual AUX TX adapter: descriptor bytes, AUX-only ownership, completion, END/flush, late/reset and failure paths PASS");
}
