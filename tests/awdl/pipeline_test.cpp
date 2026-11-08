#include "../../itlwm/awdl/FramePipeline.hpp"
#include <assert.h>
#include <initializer_list>
#include <stdint.h>
#include <stdio.h>
#include <vector>
using namespace ItlAwdl;

static const uint8_t kSelf[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
static const uint8_t kSender[6] = {0x9a, 0x11, 0x22, 0x33, 0x44, 0x55};
static const uint8_t kMaster[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x99};
static const uint8_t kChannels[16] = {153, 153, 149, 0, 0, 0, 0, 153, 6, 153, 149, 153, 0, 0, 0, 153};
static const uint8_t kOpclass[16] = {128, 128, 128, 0, 0, 0, 0, 128, 81, 128, 128, 128, 0, 0, 0, 128};

static void put16(std::vector<uint8_t> &v, uint16_t x) {
    v.push_back(uint8_t(x & 0xff)); v.push_back(uint8_t(x >> 8));
}
static void put32(std::vector<uint8_t> &v, uint32_t x) {
    for (unsigned i = 0; i < 4; ++i) v.push_back(uint8_t((x >> (8 * i)) & 0xff));
}
static void putAddr(std::vector<uint8_t> &v, const uint8_t *a) {
    v.insert(v.end(), a, a + 6);
}
static void putTlv(std::vector<uint8_t> &frame, uint8_t type, const std::vector<uint8_t> &value) {
    frame.push_back(type); put16(frame, (uint16_t)value.size());
    frame.insert(frame.end(), value.begin(), value.end());
}

// Header plus the 16-byte AWDL body; the transmitter sits in address 2.
static std::vector<uint8_t> header(const uint8_t *sender, uint8_t subtype) {
    std::vector<uint8_t> frame(24, 0);
    frame[0] = 0xd0;
    putAddr(frame, sender);                    // address 1
    for (unsigned i = 0; i < 6; ++i) frame[16 + i] = sender[i]; // address 3
    frame.resize(24);
    std::vector<uint8_t> hdr = frame;          // 24 bytes so far
    hdr[10] = sender[0]; hdr[11] = sender[1]; hdr[12] = sender[2];
    hdr[13] = sender[3]; hdr[14] = sender[4]; hdr[15] = sender[5];
    const uint8_t body[16] = {127, 0, 0x17, 0xf2, 8, 0x10, subtype, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    hdr.insert(hdr.end(), body, body + 16);
    return hdr;
}

// Real values captured from the first probe6 frame.
static std::vector<uint8_t> syncValue(uint16_t awSequence, uint16_t downCounter) {
    std::vector<uint8_t> v;
    v.push_back(6);               // next channel
    put16(v, downCounter);
    v.push_back(6);               // master channel
    v.push_back(0);               // guard time
    put16(v, 16);                 // aw period
    put16(v, 110);                // af period
    put16(v, 6144);               // flags
    put16(v, 16);                 // aw extension length
    put16(v, 16);                 // aw common length
    put16(v, 0);                  // remaining aw length
    v.push_back(3); v.push_back(3); v.push_back(3); v.push_back(3);
    putAddr(v, kMaster);
    v.push_back(4);               // presence mode
    v.push_back(0);               // reserved
    put16(v, awSequence);
    put16(v, 0);                  // ap alignment
    assert(v.size() == 33);
    return v;
}

static std::vector<uint8_t> electionV2Value() {
    std::vector<uint8_t> v;
    putAddr(v, kMaster); putAddr(v, kMaster);
    put32(v, 30397); put32(v, 1); put32(v, 521); put32(v, 510);
    put32(v, 0); put32(v, 0); put32(v, 7890);
    assert(v.size() == 40);
    return v;
}

static std::vector<uint8_t> chanSeqValue(unsigned count) {
    std::vector<uint8_t> v;
    v.push_back(uint8_t(count - 1));
    v.push_back(3);      // opclass encoding
    v.push_back(0);      // duplicates
    v.push_back(3);      // step count - 1
    put16(v, 0xffff);    // fill
    for (unsigned i = 0; i < count; ++i) { v.push_back(kChannels[i]); v.push_back(kOpclass[i]); }
    return v;
}

static std::vector<uint8_t> fullFrame(const uint8_t *sender, uint16_t awSequence, uint16_t downCounter) {
    std::vector<uint8_t> f = header(sender, 0);
    putTlv(f, 4, syncValue(awSequence, downCounter));
    putTlv(f, 24, electionV2Value());
    putTlv(f, 18, chanSeqValue(16));
    return f;
}

int main() {
    PeerTable table = {};
    peerTableReset(table);
    PipelineOutput out;
    const std::vector<uint8_t> frame = fullFrame(kSender, 35405, 41);
    FrameInput input = { frame.data(), frame.size(), 1000000, 6 };

    assert(processFrame(table, kSelf, input, out) == PipelineStatus::Ready);
    assert(out.sawSync && out.sawSequence && out.sawElection);
    assert(out.sync.awPeriod == 16 && out.sync.presenceMode == 4 && out.sync.afPeriod == 110);
    assert(out.sync.nextChannel == 6 && out.sync.masterChannel == 6);
    assert(out.sync.nextAwSequence == 35405 && out.sync.txDownCounter == 41);
    assert(out.election.v2 && out.election.masterCounter == 30397 && out.election.height == 1);
    assert(out.sequence.count == 16 && out.sequence.encoding == 3 && out.sequence.steps == 4);
    assert(out.sequence.entries[8].channel == 6 && out.sequence.entries[8].auxiliary == 81);
    assert(out.scheduleConsistent);

    // One peer in the table, updated with its advertisement.
    assert(out.peerCount == 1 && out.expiredPeers == 0);
    assert(peerSameAddress(out.peer->address, kSender));
    assert(out.peer->hasElection && out.peer->hasChannels && out.peer->sawV2);

    // The sender is one hop below a master with a much higher counter, so it wins
    // the election against us and we would sync through it.
    assert(!out.voted.isSelfMaster);
    assert(peerSameAddress(out.voted.masterAddress, kMaster));
    assert(peerSameAddress(out.voted.syncAddress, kSender));
    assert(out.voted.height == 2 && out.voted.masterCounter == 30397 && out.voted.masterMetric == 521);

    // Window and slot: 41 TU to the next boundary, EAW index 8852, slot 4, empty.
    assert(out.window.waitMicroseconds == 41 * 1024);
    assert(out.window.nextBoundaryGp2 == 1000000 + 41 * 1024);
    assert(out.window.nextBoundarySequence == 35408);
    assert(out.nextSlot.eawIndex == 8852 && out.nextSlot.slot == 4 && out.nextSlot.channel == 0);

    // A second frame from the same sender updates the entry instead of adding one.
    FrameInput second = { frame.data(), frame.size(), 1010000, 6 };
    const std::vector<uint8_t> updated = fullFrame(kSender, 35409, 10);
    second.bytes = updated.data(); second.length = updated.size();
    assert(processFrame(table, kSelf, second, out) == PipelineStatus::Ready);
    assert(out.peerCount == 1 && peerTableFind(table, kSender)->lastSeenGp2 == 1010000);
    // The counter moved to 35409, so the next boundary is 35412: EAW index 8853,
    // which is slot 5 of the same table, also empty.
    assert(out.window.nextBoundarySequence == 35412 && out.nextSlot.slot == 5);

    // A different sender joins a second entry.
    const uint8_t other[6] = {0x9a, 0xaa, 0xbb, 0xcc, 0xdd, 0xee};
    const std::vector<uint8_t> otherFrame = fullFrame(other, 35413, 20);
    FrameInput third = { otherFrame.data(), otherFrame.size(), 1020000, 149 };
    assert(processFrame(table, kSelf, third, out) == PipelineStatus::Ready);
    assert(out.peerCount == 2);

    // Frames that carry less than a full decision report it instead of guessing.
    std::vector<uint8_t> noSync = header(kSender, 0);
    putTlv(noSync, 24, electionV2Value());
    putTlv(noSync, 18, chanSeqValue(16));
    FrameInput partial = { noSync.data(), noSync.size(), 1030000, 6 };
    assert(processFrame(table, kSelf, partial, out) == PipelineStatus::NoSync);

    std::vector<uint8_t> noSeq = header(kSender, 0);
    putTlv(noSeq, 4, syncValue(35417, 30));
    FrameInput partial2 = { noSeq.data(), noSeq.size(), 1040000, 6 };
    assert(processFrame(table, kSelf, partial2, out) == PipelineStatus::NoSequence);

    // A four-entry sequence is decoded, flagged as inconsistent with the presence
    // mode, and then honestly refused: the slot this frame points at is beyond the
    // entries it advertises, so no channel is claimed for it.
    std::vector<uint8_t> shortSeq = header(kSender, 0);
    putTlv(shortSeq, 4, syncValue(35421, 30));
    putTlv(shortSeq, 18, chanSeqValue(4));
    FrameInput partial3 = { shortSeq.data(), shortSeq.size(), 1050000, 6 };
    assert(processFrame(table, kSelf, partial3, out) == PipelineStatus::NoSlot);
    assert(!out.scheduleConsistent);
    assert(out.sawSequence && out.sawSync);
    assert(out.sequence.count == 4);

    // Non-AWDL and malformed input never touch the table.
    uint8_t stranger[40] = {0x08, 0x00};
    FrameInput notAwdl = { stranger, sizeof(stranger), 1060000, 6 };
    const unsigned before = table.count;
    assert(processFrame(table, kSelf, notAwdl, out) == PipelineStatus::NotAwdl);
    const std::vector<uint8_t> truncated(frame.begin(), frame.end() - 3);
    FrameInput bad = { truncated.data(), truncated.size(), 1070000, 6 };
    assert(processFrame(table, kSelf, bad, out) == PipelineStatus::Malformed);
    assert(table.count == before);

    // Stale peers are expired before the new frame is added.
    FrameInput late = { otherFrame.data(), otherFrame.size(), 1020000 + kPeerTimeoutUs + 1, 149 };
    assert(processFrame(table, kSelf, late, out) == PipelineStatus::Ready);
    assert(out.expiredPeers >= 1);
    puts("frame pipeline: real-frame chain from envelope to slot decision PASS (ASan/UBSan)");
}
