#include "NativeAwWindow.hpp"
#include "EngineState.hpp"
#include <assert.h>
#include <string.h>

int main(){
    ItlAwdl::NativeAwWindowTracker t;
    uint32_t gp2=0;
    uint16_t aw=0;
    assert(t.observe(1000,1000,gp2,aw));
    assert(gp2==1000&&aw==1);
    assert(!t.observe(1000,1100,gp2,aw));

    const uint32_t period=ItlAwdl::NativeSyncProfile::periodUs;
    assert(t.observe(1000,1000+period,gp2,aw));
    assert(gp2==1000+period&&aw==2);
    assert(t.observe(1000,1000+period*4,gp2,aw));
    assert(gp2==1000+period*4&&aw==5);
    assert(!t.observe(2000,1000,gp2,aw));

    ItlAwdl::NativeAwWindowTracker wrap;
    wrap.number=65534;
    assert(wrap.observe(0,0,gp2,aw)&&aw==65535);
    assert(wrap.observe(0,period,gp2,aw)&&aw==1);

    const uint64_t epoch=1000000000ULL;
    const uint64_t periodNs=uint64_t(period)*1000ULL;
    assert(ItlAwdl::NativeAwWindowTracker::nextBoundaryUs(epoch,epoch)==period);
    assert(ItlAwdl::NativeAwWindowTracker::nextBoundaryUs(epoch+periodNs/2,epoch)>=
           period/2-1);
    assert(ItlAwdl::NativeAwWindowTracker::nextBoundaryUs(epoch-1,epoch)==0);

    const uint8_t mac[6]={0xd2,0xe5,0x47,0xc1,0x89,0xd7};
    uint8_t event[36];
    ItlAwdl::encodeAvailabilityWindowStart(event,mac,0x12345678,7);
    assert(!memcmp(event+8,mac,6)&&!memcmp(event+14,mac,6));
    assert(event[0]==7&&event[1]==0);
    assert(event[20]==0x12&&event[21]==0x34&&event[22]==0x56&&event[23]==0x78);
}
