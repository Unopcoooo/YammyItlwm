#include "../../itlwm/awdl/ActionFrame.hpp"
#include <assert.h>
#include <vector>
#include <stdio.h>
using namespace ItlAwdl;
static std::vector<uint8_t> frame() {
    std::vector<uint8_t> f(40, 0);
    f[0]=0xd0; f[24]=127; f[26]=0x17; f[27]=0xf2; f[28]=8; f[29]=0x10;
    return f;
}
int main() {
    Summary s; auto f=frame();
    assert(inspect(nullptr,0,s)==Result::Truncated);
    for (size_t n=0;n<f.size();++n) assert(inspect(f.data(),n,s)==Result::Truncated);
    assert(inspect(f.data(),f.size(),s)==Result::Valid && s.tlvs==0);
    f.insert(f.end(),{4,1,0,0xaa,18,0,0,24,2,0,0xbb,0xcc});
    assert(inspect(f.data(),f.size(),s)==Result::Valid && s.tlvs==3 && s.sync && s.election && s.channelSequence);
    f.pop_back(); assert(inspect(f.data(),f.size(),s)==Result::Malformed && s.tlvs==0);
    f=frame(); f.insert(f.end(),{4,0xff,0xff});
    assert(inspect(f.data(),f.size(),s)==Result::Malformed);
    f=frame(); f.push_back(4); assert(inspect(f.data(),f.size(),s)==Result::Malformed);
    f=frame(); f[30]=3; assert(inspect(f.data(),f.size(),s)==Result::Valid && s.subtype==3);
    f[30]=1; assert(inspect(f.data(),f.size(),s)==Result::Unsupported);
    f=frame(); f[29]=0x20; assert(inspect(f.data(),f.size(),s)==Result::Unsupported);
    f=frame(); f[27]=0; assert(inspect(f.data(),f.size(),s)==Result::NotAwdl);
    f=frame(); f[0]=0x80; assert(inspect(f.data(),f.size(),s)==Result::NotAwdl);
    for (uint8_t flag : {1,2,4,0x40}) { f=frame(); f[1]=flag; assert(inspect(f.data(),f.size(),s)==Result::Unsupported); }
    f=frame(); f[22]=1; assert(inspect(f.data(),f.size(),s)==Result::Unsupported);
    f=frame(); f[1]=0x80; f.insert(f.begin()+24,4,0); assert(inspect(f.data(),f.size(),s)==Result::Valid);
    // Unaligned buffer and deterministic malformed-input corpus under sanitizers.
    f=frame(); f.insert(f.begin(),0); assert(inspect(f.data()+1,f.size()-1,s)==Result::Valid);
    uint32_t rng=12345;
    for (size_t t=0;t<1000000;++t) {
        f=frame(); const size_t extra=t%256;
        for(size_t i=0;i<extra;++i) { rng=rng*1664525+1013904223; f.push_back(rng>>24); }
        inspect(f.data(),f.size(),s);
    }
    puts("AWDL envelope tests passed; 1000000 malformed-input corpus cases completed.");
}
