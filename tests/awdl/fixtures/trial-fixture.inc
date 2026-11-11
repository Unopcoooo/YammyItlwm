#include "ProtocolModel.hpp"
#include "IbssDataTrial.hpp"
#include "IbssExperiment.hpp"
#include <assert.h>
#include <stdio.h>
#include <vector>
using namespace ItlAwdl;
static const uint8_t self[6] = {2,0,0,0,0,1};
static const uint8_t sender[6] = {2,0,0,0,0,2};
static void le16(std::vector<uint8_t> &v, size_t at, unsigned value) {
    v[at] = value; v[at+1] = value >> 8;
}
static void tlv(std::vector<uint8_t> &frame, uint8_t type, const std::vector<uint8_t> &value) {
    frame.push_back(type); frame.push_back(value.size() & 255); frame.push_back(value.size() >> 8);
    frame.insert(frame.end(), value.begin(), value.end());
}
// Synthetic wire fixture reconstructed from probe6's exported scalar values.
// NOT a captured packet: addresses and fields not exported by probe6 are invented.
static std::vector<uint8_t> fixture(bool reverse = false, bool v2 = true) {
    std::vector<uint8_t> f(40, 0);
    f[0] = 0xd0; memcpy(f.data()+10, sender, 6);
    f[24]=127; f[26]=0x17; f[27]=0xf2; f[28]=8; f[29]=0x10; f[30]=3;
    std::vector<uint8_t> sync(33,0);
    sync[0]=6; le16(sync,1,41); sync[3]=6; le16(sync,5,16); le16(sync,7,110);
    le16(sync,9,6144); le16(sync,11,16); le16(sync,13,16);
    sync[17]=sync[18]=sync[19]=sync[20]=3; sync[27]=4; le16(sync,29,35405);
    std::vector<uint8_t> channels(41,0);
    channels[0]=15; channels[1]=3; channels[3]=3; channels[4]=channels[5]=255;
    const uint8_t numbers[16]={153,153,149,0,0,0,0,153,6,153,149,153,0,0,0,153};
    for(unsigned i=0;i<16;++i) {
        channels[6+2*i]=numbers[i]; channels[7+2*i]=numbers[i] ? (numbers[i]==6 ? 81 : 128) : 0;
    }
    std::vector<uint8_t> one(19,0), two(40,0);
    one[3]=1; one[11]=9; one[12]=2; // 521
    two[12]=0xbd; two[13]=0x76; // 30397
    two[16]=1; two[20]=9; two[21]=2; two[24]=0xfe; two[25]=1;
    two[36]=0xd2; two[37]=0x1e; // 7890
    tlv(f,4,sync); tlv(f,18,channels);
    if (reverse && v2) tlv(f,24,two);
    tlv(f,5,one);
    if (!reverse && v2) tlv(f,24,two);
    return f;
}

struct Counter {bool modelSeen=true;uint32_t modelGeneration=6,modelEpoch=3;uint64_t modelLastNs=1000000000;PeerTable modelPeers={};ItlIbss::ExperimentObservation ibss;
    bool planIbssWindow(uint32_t generation,uint32_t gp2,uint64_t host,uint64_t lease,
                        const uint8_t self[6],ItlIbss::WindowPlan &out,uint32_t &reason,const uint8_t *target=nullptr) {
        out={};reason=1;
        if(!modelSeen || generation!=modelGeneration || host<modelLastNs || host-modelLastNs>500000000ull)return false;
        ItlAwdl::ProtocolPrediction prediction={};reason=2;
        const ItlAwdl::Peer *peer=nullptr;
        if(target){
            peer=ItlAwdl::peerTableFind(modelPeers,target);
            if(!peer||!peer->hasSync||!peer->hasChannels||
               !ItlAwdl::predictWindowWithTxDelay(peer->sync,peer->syncReceivedGp2,gp2,500000,peer->timing.txDelayUs,prediction.window))return false;
        } else {
            if(!ItlAwdl::predictProtocol(modelPeers,self,gp2,prediction))return false;
            peer=ItlAwdl::peerTableFind(modelPeers,prediction.election.syncAddress);
        }
        if(!peer)return false;reason=3;
        if(!peer->timing.consistent || peer->timing.streak<3)return false;
        const uint32_t period=uint32_t(peer->sync.awPeriod)*peer->sync.presenceMode*1024;
        reason=4;
        for(unsigned i=0;i<8;++i) {
            auto w=prediction.window;w.nextBoundaryGp2+=i*period;
            w.nextBoundarySequence=uint16_t(w.nextBoundarySequence+i*peer->sync.presenceMode);
            ItlAwdl::SlotView slot={};if(!ItlAwdl::predictNextSlot(peer->sync,peer->channels,w,slot) || slot.channel!=6)continue;
            const uint32_t duration=period-4096;
            const uint32_t open=w.nextBoundaryGp2+2048,close=w.nextBoundaryGp2+period-2048;
            const auto lead=ItlIbss::windowDelta(open,gp2);
            if(lead<20000 || lead>250000 || uint64_t(gp2-peer->syncReceivedGp2)+uint64_t(lead)+duration>500000)continue;
            if(host+uint64_t(lead+duration+5000)*1000>=lease){reason=5;return false;}
            out.version=2;out.generation=generation;out.peer=peer->diagnosticId;out.epoch=modelEpoch;
            out.boundary=w.nextBoundaryGp2;out.period=period;out.streak=peer->timing.streak;
            out.observedGp2=peer->syncReceivedGp2;out.observedHost=modelLastNs;out.open=open;out.close=close;
            out.rawAwSequence=peer->sync.nextAwSequence;out.txDelayUs=peer->timing.txDelayUs;
            out.awPeriod=peer->sync.awPeriod;out.presence=peer->sync.presenceMode;out.downCounter=peer->sync.txDownCounter;
            out.boundarySequence=w.nextBoundarySequence;out.selectedSlot=slot.slot;out.candidateOffset=i;
            out.sequenceCount=peer->channels.count;out.sequenceSteps=peer->channels.steps;out.sequenceFill=peer->channels.fill;
            out.sequenceEncoding=peer->channels.encoding;out.sequenceDuplicates=peer->channels.duplicates;
            for(unsigned j=0;j<16;++j)out.sequenceEntries[j]=uint16_t(peer->channels.entries[j].channel)|(uint16_t(peer->channels.entries[j].auxiliary)<<8);
            reason=0;return true;
        }
        return false;
    }
void dataModelView(ItlAwdl::DataContext &c){c.peers=&modelPeers;c.modelGeneration=modelGeneration;c.modelEpoch=modelEpoch;c.modelHostNs=modelLastNs;}
};
