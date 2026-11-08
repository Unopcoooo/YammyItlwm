#include "../../itlwm/awdl/ProtocolModel.hpp"
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
int main() {
    // Exercise audit state through the actual bounded wire ingest, per sender.
    PeerTable audited={};auto timed=fixture();
    assert(ingestAdvertisement(audited,self,timed.data(),timed.size(),100000)==IngestResult::Accepted);
    le16(timed,44,40);assert(ingestAdvertisement(audited,self,timed.data(),timed.size(),101024)==IngestResult::Accepted);
    le16(timed,44,39);assert(ingestAdvertisement(audited,self,timed.data(),timed.size(),102048)==IngestResult::Accepted);
    assert(audited.peers[0].timing.streak==3&&audited.peers[0].timing.consistent);
    timed[15]=3;assert(ingestAdvertisement(audited,self,timed.data(),timed.size(),102048)==IngestResult::Accepted);
    assert(audited.peers[1].timing.streak==1&&audited.peers[0].timing.streak==3);
    Advertisement advert;
    auto frame=fixture();
    assert(decodeAdvertisement(frame.data(),frame.size(),advert)==AdvertisementResult::Valid);
    assert(advert.election.v2 && advert.election.masterCounter==30397 && advert.channels.entries[8].channel==6);
    auto reverse=fixture(true);
    assert(decodeAdvertisement(reverse.data(),reverse.size(),advert)==AdvertisementResult::Valid && advert.election.v2);
    PeerTable table={};
    assert(ingestAdvertisement(table,self,frame.data(),frame.size(),1000000)==IngestResult::Accepted);
    ProtocolPrediction prediction;
    assert(predictProtocol(table,self,1000000,prediction));
    assert(prediction.window.nextBoundaryGp2==1041984 && prediction.slot.slot==4 && prediction.slot.channel==0);
    // Three boundaries after that empty slot: slot 7 is channel 153.
    assert(predictProtocol(table,self,1173056,prediction));
    assert(prediction.slot.slot==7 && prediction.slot.channel==153);
    auto before=table.peers[0];
    auto broken=frame; tlv(broken,24,std::vector<uint8_t>(40,0));
    assert(decodeAdvertisement(broken.data(),broken.size(),advert)==AdvertisementResult::Duplicate);
    assert(ingestAdvertisement(table,self,broken.data(),broken.size(),1200000)==IngestResult::Rejected);
    assert(memcmp(&before,&table.peers[0],sizeof(Peer))==0);
    broken=frame; tlv(broken,5,std::vector<uint8_t>(1,0));
    assert(decodeAdvertisement(broken.data(),broken.size(),advert)==AdvertisementResult::Duplicate);
    broken=fixture(true); broken.resize(broken.size()-19); // v1's advertised length now exceeds the envelope
    assert(decodeAdvertisement(broken.data(),broken.size(),advert)==AdvertisementResult::Envelope);
    auto downgrade=fixture(false,false);
    assert(ingestAdvertisement(table,self,downgrade.data(),downgrade.size(),1300000)==IngestResult::Downgrade);
    assert(table.peers[0].lastSeenGp2==1000000);
    broken=frame; broken[10]=1;
    assert(decodeAdvertisement(broken.data(),broken.size(),advert)==AdvertisementResult::Sender);
    broken=frame; memcpy(broken.data()+10,self,6);
    assert(ingestAdvertisement(table,self,broken.data(),broken.size(),1300000)==IngestResult::Self);
    assert(!predictProtocol(table,self,3000001,prediction) && table.count==0);
    assert(prediction.window.waitMicroseconds==0 && prediction.slot.channel==0);
    // GP2 wrap is handled with unsigned ages, as long as the caller resets on firmware reset.
    assert(ingestAdvertisement(table,self,frame.data(),frame.size(),0xfffffff0u)==IngestResult::Accepted);
    assert(predictProtocol(table,self,0x10u,prediction));
    assert(prediction.window.waitMicroseconds==41*1024-32);
    // A rejected, incomplete advertisement does not allocate a peer.
    peerTableReset(table); broken=frame; broken.resize(40);
    assert(ingestAdvertisement(table,self,broken.data(),broken.size(),1000)==IngestResult::Rejected && !table.count);
    for(unsigned i=0;i<kMaxPeers;++i) {
        broken=frame; broken[15]=i+2;
        assert(ingestAdvertisement(table,self,broken.data(),broken.size(),1000)==IngestResult::Accepted);
    }
    broken=frame; broken[15]=100;
    assert(ingestAdvertisement(table,self,broken.data(),broken.size(),1000)==IngestResult::Full);
    assert(ingestAdvertisement(table,self,broken.data(),broken.size(),2001001)==IngestResult::Accepted && table.count==1);
    puts("protocol pipeline: atomic ingestion, election, expiry, slots and GP2 wrap PASS (ASan/UBSan)");
}
