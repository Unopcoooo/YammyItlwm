#include "../../itlwm/awdl/SyncParameters.hpp"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <vector>
using namespace ItlAwdl;
int main() {
    // Independently laid out fixed value, with distinct endian-sensitive fields.
    const uint8_t bytes[] = {
        6, 0x34,0x12, 44, 3, 0x10,0, 0x20,0, 0xcd,0xab,
        0x30,0, 0x40,0, 0x50,0, 1,2,3,4,
        0x02,0x11,0x22,0x33,0x44,0x55, 4,0, 0xfe,0xff, 0x78,0x56
    };
    static_assert(sizeof(bytes)==33,"fixture fixed-layout size");
    SyncParameters s = {};
    assert(!decodeSyncParameters(nullptr,33,s));
    for(size_t n=0;n<sizeof(bytes);++n) {
        s.awPeriod=123;
        assert(!decodeSyncParameters(bytes,n,s) && s.awPeriod==0);
    }
    assert(decodeSyncParameters(bytes,sizeof(bytes),s));
    assert(s.nextChannel==6 && s.masterChannel==44 && s.guardTime==3);
    assert(s.txDownCounter==0x1234 && s.awPeriod==16 && s.afPeriod==32);
    assert(s.flags==0xabcd && s.awExtensionLength==48 && s.awCommonLength==64);
    assert(s.remainingAwLength==80 && s.minExtension==1 && s.maxMulticastExtension==2);
    assert(s.maxUnicastExtension==3 && s.maxActionExtension==4);
    assert(s.masterAddress[0]==2 && s.masterAddress[5]==0x55 && s.presenceMode==4);
    assert(s.nextAwSequence==0xfffe && s.apAlignment==0x5678);
    uint8_t unaligned[35]={};for(size_t i=0;i<33;++i)unaligned[1+i]=bytes[i];
    assert(decodeSyncParameters(unaligned+1,34,s) && s.txDownCounter==0x1234);
    uint8_t zeros[33]={};assert(decodeSyncParameters(zeros,33,s) && s.awPeriod==0);
    // Zero is structurally decodable, not an instruction to schedule a zero period.
    std::vector<uint8_t> frame(40,0);
    frame[0]=0xd0;frame[24]=127;frame[26]=0x17;frame[27]=0xf2;frame[28]=8;frame[29]=0x10;
    assert(extractSyncParameters(frame.data(),frame.size(),s)==SyncExtraction::NoSync);
    frame.insert(frame.end(),{4,33,0});frame.insert(frame.end(),bytes,bytes+33);
    assert(extractSyncParameters(frame.data(),frame.size(),s)==SyncExtraction::Found && s.awPeriod==16);
    auto shortFrame=frame;shortFrame.pop_back();
    assert(extractSyncParameters(shortFrame.data(),shortFrame.size(),s)==SyncExtraction::InvalidEnvelope && s.awPeriod==0);
    shortFrame[41]=32;
    assert(extractSyncParameters(shortFrame.data(),shortFrame.size(),s)==SyncExtraction::MalformedSync);
    frame.insert(frame.end(),{4,33,0});frame.insert(frame.end(),bytes,bytes+33);
    assert(extractSyncParameters(frame.data(),frame.size(),s)==SyncExtraction::DuplicateSync && s.awPeriod==0);
    puts("Sync fixed-value decoder: truncation, unaligned access, endian and field tests passed.");
}
