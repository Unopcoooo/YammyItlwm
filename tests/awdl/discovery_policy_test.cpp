#include "../../itlwm/awdl/DiscoveryPolicy.hpp"
#include <cassert>
#include <cstdio>
int main(){
    ItlAwdl::DiscoveryPolicy p;uint32_t channel,duration;uint64_t now=1;
    assert(!p.candidate(now,31,channel,duration));
    assert(p.candidate(now,32,channel,duration)&&duration==22&&channel==6);
    assert(p.candidate(now,64,channel,duration)&&duration==54);
    assert(p.candidate(now,74,channel,duration)&&duration==64);
    assert(p.candidate(now,300,channel,duration)&&duration==64);
    for(unsigned n=0;n<24;++n){
        if(n && !(n%4)) {assert(!p.candidate(now,100,channel,duration));now+=60000000000ULL;}
        assert(p.candidate(now,100,channel,duration));
        const unsigned channels[]={6,149,44};assert(channel==channels[(n/4)%3]);
        p.submitted(now,true);assert(p.attempts==n+1&&p.aligned==0);
    }
    assert(!p.candidate(now+60000000000ULL,100,channel,duration));
    for(unsigned n=0;n<24;++n){assert(!p.exhausted(24+n));p.submitted(now,false);}
    assert(p.exhausted(48)&&p.aligned==24);
    puts("discovery policy: DTIM headroom, channel bursts, cooldown, discovery/total/alignment bounds PASS");
}
