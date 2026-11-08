#include "../../itlwm/awdl/AuxTxRepeat.hpp"
#include <cassert>
#include <cstdio>
using ItlAwdl::AuxTxRepeat;
using ItlAwdl::AuxTxPhase;
using S = AuxTxRepeat::Stop;
int main() {
    AuxTxRepeat invalid;
    assert(!invalid.arm(7,1,100,100,1));
    assert(!invalid.arm(7,1,100,101,0));
    assert(!invalid.arm(7,1,100,101,25));
    assert(!invalid.arm(7,32,100,101,1));
    assert(!invalid.arm(7,1,100,100 + AuxTxRepeat::MaxDurationNs + 1,1));
    assert(invalid.arm(7,1,100,10000,24));
    assert(!invalid.arm(7,1,100,10000,24));
    // Twenty-four real ownership transitions, ring wrap 255 -> 0 included.
    for (unsigned i=0;i<24;++i) {
        unsigned slot=(250+i)%256; uint64_t now=100+i*10;
        assert(invalid.begin(7,now,i+1,1,slot,true));
        assert(!invalid.begin(7,now,i+2,1,(slot+1)%256,true));
        assert(invalid.submit(7,now+1,now+8));
        assert(!invalid.submit(7,now+1,now+8));
        assert(!invalid.begin(7,now+2,i+2,1,(slot+1)%256,true));
        assert(!invalid.complete(8,1,slot));
        assert(!invalid.complete(7,2,slot));
        assert(!invalid.complete(7,1,(slot+1)%256));
        assert(invalid.complete(7,1,slot));
        assert(!invalid.complete(7,1,slot));
    }
    assert(!invalid.begin(7,500,25,1,18,true));
    assert(invalid.reason()==S::Budget && invalid.attempts()==24);
    assert(!invalid.arm(7,1,500,20000,24));
    // Busy ring / stale window / bad queue consume no budget.
    AuxTxRepeat busy; assert(busy.arm(3,1,10,1000,3));
    assert(!busy.begin(3,11,1,1,0,false));
    assert(!busy.begin(3,11,0,1,0,true));
    assert(!busy.begin(3,11,1,2,0,true));
    assert(!busy.begin(3,11,1,1,256,true));
    assert(busy.attempts()==0 && busy.begin(3,12,1,1,0,true));
    assert(busy.skipped() && !busy.skipped());
    assert(!busy.begin(3,13,1,1,1,true));
    assert(busy.begin(3,14,2,1,0,true)); // unpublished failed preparation is reusable
    assert(busy.submit(3,14,100) && busy.complete(3,1,0));
    assert(!busy.begin(3,14,3,1,0,true)); // never reuse a published retired slot
    assert(busy.begin(3,14,3,1,1,true));
    assert(!busy.submit(3,14,1001)); // no deadline extension
    assert(busy.submit(3,15,100));
    assert(busy.requestFlush(3,1100)); // cleanup may outlive admission window
    assert(!busy.requestFlush(3,1200));
    busy.quarantine(); assert(busy.ownership().pending);
    assert(busy.complete(3,1,1) && !busy.ownership().pending);
    assert(!busy.begin(3,16,3,1,2,true) && busy.reason()==S::Quarantined);
    // Stop does not fabricate DMA completion or free resources.
    for (unsigned failure=0;failure<4;++failure) {
        AuxTxRepeat p; assert(p.arm(4,1,100,200,2));
        assert(p.begin(4,101,1,1,7,true) && p.submit(4,102,150));
        if(failure==0) p.cancel();
        if(failure==1) assert(!p.poll(4,200));
        if(failure==2) assert(!p.poll(5,103));
        if(failure==3) assert(!p.poll(4,99));
        const S expected[]={S::Cancelled,S::Deadline,S::Generation,S::Clock};
        assert(p.reason()==expected[failure] && p.ownership().pending);
        assert(!p.begin(4,104,2,1,8,true));
        assert(p.complete(4,1,7) && !p.ownership().pending);
        assert(!p.poll(4,105));
        p.deviceQuiesced(); assert(!p.arm(5,1,110,300,2));
    }
    AuxTxRepeat reset; assert(reset.arm(1,1,100,200,1));
    assert(reset.begin(1,101,1,1,0,true) && reset.submit(1,102,150));
    reset.deviceQuiesced(); assert(!reset.ownership().pending && reset.reason()==S::DeviceStopped);
    assert(!reset.complete(1,1,0) && !reset.arm(2,1,110,300,1));
    AuxTxRepeat expiry; assert(expiry.arm(1,1,100,200,1));
    assert(expiry.begin(1,101,1,1,0,true));
    assert(!expiry.submit(1,200,201) && expiry.reason()==S::Deadline);
    assert(expiry.skipped() && !expiry.ownership().pending);
    // Failure/skips count toward the finite budget as well as successful TX.
    AuxTxRepeat skips; assert(skips.arm(1,1,100,200,2));
    assert(skips.begin(1,101,1,1,0,true) && skips.skipped());
    assert(skips.begin(1,102,2,1,1,true) && skips.skipped());
    assert(!skips.begin(1,103,3,1,2,true) && skips.reason()==S::Budget);
    puts("PASS: bounded AUX repetition admissions, real ownership transitions, wrap/no slot reuse, busy/skip budgets, deadline/generation/clock/cancel stops and late completion without restart");
}
