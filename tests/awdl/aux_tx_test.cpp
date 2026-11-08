#include "../../itlwm/awdl/AuxTx.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>
using namespace ItlAwdl;
int main() {
    AuxTxOwnership state;
    state.deviceQuiesced(); // initial firmware init must not suppress the first attempt
    assert(!state.submit(1,1,0,100) && state.attempt() && !state.attempt());
    assert(state.submit(1,1,255,100) && state.pending);
    assert(!state.complete(2,1,255) && !state.complete(1,0,255) && !state.complete(1,1,0));
    assert(state.requestFlush(1,200) && !state.requestFlush(1,300));
    state.quarantine(); assert(state.pending); // never free DMA just because a timeout occurred
    assert(state.complete(1,1,255) && !state.pending && !state.attempt());
    AuxTxOwnership reset; assert(reset.attempt() && reset.submit(1,1,0,100));
    reset.deviceQuiesced(); assert(!reset.pending && !reset.attempt());
    AuxTxOwnership skipped; assert(skipped.attempt()); skipped.skipped(); assert(!skipped.attempt());
    assert(auxTxWindowOpen(1,1,16,0xfffffff0,true));
    assert(auxTxWindowOpen(1,1,2000,0,true));
    assert(!auxTxWindowOpen(1,1,2001,0,true));
    assert(!auxTxWindowOpen(1,1,99,100,true));
    assert(!auxTxWindowOpen(1,2,100,100,true));
    assert(!auxTxWindowOpen(1,1,100,100,false));
    assert(auxUnencryptedSchedulerEntry(40,1)==0x100c);
    uint8_t storage[42]; memset(storage,0xa5,sizeof(storage));
    const uint8_t source[6]={2,1,2,3,4,5};
    assert(buildAuxTxProbe(source,0x12345678,storage+1));
    assert(storage[0]==0xa5 && storage[41]==0xa5);
    const uint8_t expected[40]={0xd0,0,0,0,255,255,255,255,255,255,2,1,2,3,4,5,
        0,0x25,0,0xff,0x94,0x73,0,0,127,0,0x17,0xf2,8,0x10,0,0,
        0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12};
    assert(memcmp(storage+1,expected,40)==0);
    Summary summary; assert(inspect(storage+1,40,summary)==Result::Valid && summary.tlvs==0);
    puts("AUX TX: ownership, delayed completion, quarantine/reset, GP2 gate and independent 40-byte fixture PASS");
}
