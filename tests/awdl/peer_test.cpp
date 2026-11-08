#include "../../itlwm/awdl/PeerTable.hpp"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <vector>
using namespace ItlAwdl;

static void putTlv(std::vector<uint8_t> &frame, uint8_t type, const std::vector<uint8_t> &value) {
    frame.push_back(type);
    frame.push_back(uint8_t(value.size() & 0xff));
    frame.push_back(uint8_t((value.size() >> 8) & 0xff));
    frame.insert(frame.end(), value.begin(), value.end());
}

static std::vector<uint8_t> header() {
    std::vector<uint8_t> frame(40, 0);
    frame[0] = 0xd0; frame[24] = 127; frame[26] = 0x17; frame[27] = 0xf2;
    frame[28] = 8; frame[29] = 0x10;
    return frame;
}

static std::vector<uint8_t> electionV1(uint32_t metric, uint32_t selfMetric, uint8_t height) {
    std::vector<uint8_t> v(19, 0);
    v[3] = height;
    const uint8_t master[6] = {0xaa, 1, 2, 3, 4, 5};
    for (unsigned i = 0; i < 6; ++i) v[5 + i] = master[i];
    for (unsigned i = 0; i < 4; ++i) {
        v[11 + i] = uint8_t((metric >> (8 * i)) & 0xff);
        v[15 + i] = uint8_t((selfMetric >> (8 * i)) & 0xff);
    }
    return v;
}

static std::vector<uint8_t> electionV2(const uint8_t *master, const uint8_t *sync,
                                       uint32_t counter, uint32_t height, uint32_t metric,
                                       uint32_t selfCounter) {
    std::vector<uint8_t> v(40, 0);
    for (unsigned i = 0; i < 6; ++i) { v[i] = master[i]; v[6 + i] = sync[i]; }
    for (unsigned i = 0; i < 4; ++i) {
        v[12 + i] = uint8_t((counter >> (8 * i)) & 0xff);
        v[16 + i] = uint8_t((height >> (8 * i)) & 0xff);
        v[20 + i] = uint8_t((metric >> (8 * i)) & 0xff);
        v[24 + i] = uint8_t((metric >> (8 * i)) & 0xff);
        v[36 + i] = uint8_t((selfCounter >> (8 * i)) & 0xff);
    }
    return v;
}

static const uint8_t kSelf[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
static const uint8_t kMasterA[6] = {0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa};
static const uint8_t kPeerLow[6] = {0x10, 0, 0, 0, 0, 0x01};
static const uint8_t kPeerHigh[6] = {0x20, 0, 0, 0, 0, 0x02};
static const uint8_t kPeerTiny[6] = {0x00, 0, 0, 0, 0, 0x01};

static void setElection(PeerTable &table, const uint8_t *address, uint32_t counter,
                        uint32_t metric, uint32_t height, const uint8_t *sync,
                        uint32_t nowGp2 = 1) {
    Peer *peer = peerTableTouch(table, address, nowGp2);
    assert(peer);
    peer->election = ElectionParameters();
    memcpy(peer->election.masterAddress, kMasterA, 6);
    if (sync) memcpy(peer->election.syncAddress, sync, 6);
    peer->election.masterCounter = counter;
    peer->election.masterMetric = metric;
    peer->election.height = height;
    peer->election.v2 = sync != nullptr;
    peer->hasElection = true;
}

int main() {
    // ---- election TLV values ----
    ElectionParameters e = {};
    const std::vector<uint8_t> v1 = electionV1(0x12345678, 0x0badf00d, 3);
    assert(!decodeElectionV1(nullptr, 19, e));
    for (size_t n = 0; n < v1.size(); ++n) {
        e.masterMetric = 7;
        assert(!decodeElectionV1(v1.data(), n, e) && e.masterMetric == 0);
    }
    assert(decodeElectionV1(v1.data(), v1.size(), e));
    assert(e.height == 3 && e.masterMetric == 0x12345678 && e.selfMetric == 0x0badf00d);
    assert(e.masterAddress[0] == 0xaa && e.masterAddress[5] == 5 && !e.v2);
    assert(e.masterCounter == 0 && e.syncAddress[0] == 0 && e.selfCounter == 0);

    std::vector<uint8_t> unalignedV1(20, 0);
    for (size_t i = 0; i < v1.size(); ++i) unalignedV1[1 + i] = v1[i];
    assert(decodeElectionV1(unalignedV1.data() + 1, 19, e) && e.height == 3);

    const uint8_t syncB[6] = {0xbb, 0, 0, 0, 0, 1};
    const std::vector<uint8_t> v2 = electionV2(kMasterA, syncB, 0x0000bEEF, 5, 0x01020304, 0x99);
    assert(!decodeElectionV2(nullptr, 40, e));
    for (size_t n = 0; n < v2.size(); ++n) assert(!decodeElectionV2(v2.data(), n, e));
    assert(decodeElectionV2(v2.data(), v2.size(), e));
    assert(e.v2 && e.masterCounter == 0x0000beef && e.height == 5);
    assert(e.masterMetric == 0x01020304 && e.selfCounter == 0x99);
    assert(e.masterAddress[0] == 0xaa && e.syncAddress[0] == 0xbb);

    // ---- extraction from complete frames ----
    std::vector<uint8_t> frame = header();
    assert(extractElectionParameters(frame.data(), frame.size(), e) == ElectionExtraction::None);
    putTlv(frame, 4, std::vector<uint8_t>(33, 0));
    assert(extractElectionParameters(frame.data(), frame.size(), e) == ElectionExtraction::None);
    putTlv(frame, 5, v1);
    assert(extractElectionParameters(frame.data(), frame.size(), e) == ElectionExtraction::Found);
    assert(!e.v2 && e.masterMetric == 0x12345678);
    putTlv(frame, 24, v2);
    assert(extractElectionParameters(frame.data(), frame.size(), e) == ElectionExtraction::Found);
    assert(e.v2 && e.masterCounter == 0x0000beef);

    std::vector<uint8_t> onlyV2 = header();
    putTlv(onlyV2, 24, v2);
    assert(extractElectionParameters(onlyV2.data(), onlyV2.size(), e) == ElectionExtraction::Found && e.v2);

    std::vector<uint8_t> badAfterV2 = onlyV2;
    putTlv(badAfterV2, 5, std::vector<uint8_t>(1, 0));
    assert(extractElectionParameters(badAfterV2.data(), badAfterV2.size(), e) == ElectionExtraction::Malformed);
    assert(e.masterCounter == 0);
    badAfterV2 = onlyV2;
    putTlv(badAfterV2, 24, v2);
    assert(extractElectionParameters(badAfterV2.data(), badAfterV2.size(), e) == ElectionExtraction::Malformed);

    std::vector<uint8_t> malformed = header();
    putTlv(malformed, 24, std::vector<uint8_t>(39, 0));
    assert(extractElectionParameters(malformed.data(), malformed.size(), e) ==
           ElectionExtraction::Malformed);

    std::vector<uint8_t> truncated = onlyV2;
    truncated.pop_back();
    assert(extractElectionParameters(truncated.data(), truncated.size(), e) ==
           ElectionExtraction::InvalidEnvelope);

    // ---- peer table ----
    PeerTable table = {};
    peerTableReset(table);
    assert(peerTableTouch(table, kPeerLow, 100) != nullptr && table.count == 1);
    assert(peerTableTouch(table, kPeerHigh, 200) != nullptr && table.count == 2);
    assert(peerTableTouch(table, kPeerLow, 300) != nullptr && table.count == 2);
    assert(peerTableFind(table, kPeerLow)->lastSeenGp2 == 300);
    assert(peerTableFind(table, kMasterA) == nullptr);
    for (unsigned i = table.count; i < kMaxPeers; ++i) {
        uint8_t address[6] = {0x40, 0, 0, 0, 0, uint8_t(i)};
        assert(peerTableTouch(table, address, 1000) != nullptr);
    }
    assert(table.count == kMaxPeers);
    const uint8_t overflow[6] = {0x50, 0, 0, 0, 0, 0xff};
    assert(peerTableTouch(table, overflow, 1000) == nullptr && table.count == kMaxPeers);

    peerTableReset(table);
    assert(peerTableTouch(table, kPeerLow, 1000) != nullptr);
    assert(peerTableAge(table, 2000, 1000) == 0 && table.count == 1);  // age == timeout stays
    assert(peerTableAge(table, 2001, 1000) == 1 && table.count == 0);  // age > timeout expires

    peerTableReset(table);
    assert(peerTableTouch(table, kPeerLow, 0xffffff00u) != nullptr);
    assert(peerTableAge(table, 0x100u, 1000) == 0);        // wrapped age 512
    assert(peerTableAge(table, 0x400u, 1000) == 1);        // wrapped age 1280

    // ---- election ----
    peerTableReset(table);
    ElectionView view = electSyncMaster(table, kSelf);
    assert(view.isSelfMaster && peerSameAddress(view.masterAddress, kSelf) && view.height == 0);
    assert(view.masterMetric == kElectionMetricInit && view.masterCounter == kElectionCounterInit);

    // A peer must beat our own starting counter/metric to be adopted.
    peerTableReset(table);
    setElection(table, kPeerTiny, kElectionCounterInit, kElectionMetricInit - 1, 0, nullptr);
    assert(electSyncMaster(table, kSelf).isSelfMaster);
    // Equal counter and metric, equal height: only an address above ours wins,
    // so a lower address leaves us as our own master.
    setElection(table, kPeerTiny, kElectionCounterInit, kElectionMetricInit, 0, nullptr);
    assert(electSyncMaster(table, kSelf).isSelfMaster);
    peerTableReset(table);
    setElection(table, kPeerLow, kElectionCounterInit, kElectionMetricInit, 0, nullptr);
    assert(!electSyncMaster(table, kSelf).isSelfMaster);

    peerTableReset(table);
    setElection(table, kPeerLow, 7, 10, 2, syncB);
    view = electSyncMaster(table, kSelf);
    assert(!view.isSelfMaster && peerSameAddress(view.masterAddress, kMasterA) &&
           peerSameAddress(view.syncAddress, kPeerLow) && view.height == 3 &&
           view.masterCounter == 7 && view.masterMetric == 10);

    // Higher counter wins outright; equal counter falls back to the metric.
    peerTableReset(table);
    setElection(table, kPeerLow, 8, 1, 1, syncB);
    setElection(table, kPeerHigh, 7, 9999, 1, syncB);
    assert(peerSameAddress(electSyncMaster(table, kSelf).syncAddress, kPeerLow));
    peerTableReset(table);
    setElection(table, kPeerHigh, 7, 9999, 1, syncB);
    setElection(table, kPeerLow, 7, 1, 1, syncB);
    assert(peerSameAddress(electSyncMaster(table, kSelf).syncAddress, kPeerHigh));

    // Equal counter and metric: shorter distance wins, then the larger address.
    peerTableReset(table);
    setElection(table, kPeerHigh, 7, 10, 4, syncB);
    setElection(table, kPeerLow, 7, 10, 1, syncB);
    assert(peerSameAddress(electSyncMaster(table, kSelf).syncAddress, kPeerLow));
    peerTableReset(table);
    setElection(table, kPeerLow, 7, 10, 2, syncB);
    setElection(table, kPeerHigh, 7, 10, 2, syncB);
    assert(peerSameAddress(electSyncMaster(table, kSelf).syncAddress, kPeerHigh));

    // Equal master rank must be independent of peer insertion order. A peer
    // one level farther away must not beat the selected peer via its address.
    peerTableReset(table);
    setElection(table, kPeerLow, 7, 10, 1, syncB);
    setElection(table, kPeerHigh, 7, 10, 2, syncB);
    assert(peerSameAddress(electSyncMaster(table, kSelf).syncAddress, kPeerLow));
    peerTableReset(table);
    setElection(table, kPeerHigh, 7, 10, 2, syncB);
    setElection(table, kPeerLow, 7, 10, 1, syncB);
    assert(peerSameAddress(electSyncMaster(table, kSelf).syncAddress, kPeerLow));

    // Height limit and cycle rejection.
    peerTableReset(table);
    setElection(table, kPeerLow, 99, 99, kElectionTreeMaxHeight, syncB);
    assert(electSyncMaster(table, kSelf).isSelfMaster);
    setElection(table, kPeerLow, 99, 99, kElectionTreeMaxHeight - 1, syncB);
    assert(!electSyncMaster(table, kSelf).isSelfMaster);
    peerTableReset(table);
    setElection(table, kPeerLow, 99, 99, 1, kSelf);
    assert(electSyncMaster(table, kSelf).isSelfMaster);
    peerTableReset(table);
    setElection(table, kPeerLow, 1, 1, 1, nullptr);      // v1: no sync address to check
    assert(!electSyncMaster(table, kSelf).isSelfMaster);

    // A peer without an election TLV never wins, even after being touched.
    peerTableReset(table);
    assert(peerTableTouch(table, kPeerLow, 5) != nullptr);
    assert(electSyncMaster(table, kSelf).isSelfMaster);
    puts("peer table, election decoding and sync-tree election: PASS (ASan/UBSan)");
}
