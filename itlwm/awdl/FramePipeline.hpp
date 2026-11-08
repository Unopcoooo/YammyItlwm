// SPDX-License-Identifier: GPL-2.0-or-later
// One AWDL sync frame in, one scheduling decision out.
// This is the integration seam the driver will call from the receive path once a
// transmit and dwell capability exists: it walks the TLVs once, updates the peer
// table, runs the election, and predicts the next availability window and slot.
// Offline arithmetic and bookkeeping only: no radio I/O, no allocation, no
// timers. Every status other than Ready means the frame contributed less than a
// full decision, and the output fields that were filled are still valid.
#pragma once
#include "ChannelSchedule.hpp"
#include "ChannelSequence.hpp"
#include "ElectionParameters.hpp"
#include "PeerTable.hpp"
#include "SyncParameters.hpp"

namespace ItlAwdl {

struct FrameInput {
    const uint8_t *bytes;
    size_t length;
    uint32_t receiveGp2;   // same microsecond clock the window model expects
    uint8_t receiveChannel;
};

enum class PipelineStatus : uint8_t {
    NotAwdl, Malformed, Unsupported, TableFull,
    NoSync, SyncUnusable, NoSequence, NoSlot, Ready
};

struct PipelineOutput {
    SyncParameters sync = {};
    ElectionParameters election = {};
    ChannelSequence sequence = {};
    WindowPrediction window = {};
    SlotView nextSlot = {};
    ElectionView voted = {};
    Peer *peer = nullptr;
    unsigned peerCount = 0;
    unsigned expiredPeers = 0;
    bool sawSync = false;
    bool sawSequence = false;
    bool sawElection = false;
    // Whether the sender's table agrees with its own presence mode. The reference
    // implementation would reject such a peer; here it is reported, not enforced.
    bool scheduleConsistent = false;
};

inline PipelineStatus processFrame(PeerTable &table, const uint8_t *selfAddress,
                                   const FrameInput &input, PipelineOutput &out) {
    out = PipelineOutput();
    if (!input.bytes) return PipelineStatus::Malformed;
    Summary envelope;
    switch (inspect(input.bytes, input.length, envelope)) {
    case Result::NotAwdl: return PipelineStatus::NotAwdl;
    case Result::Truncated:
    case Result::Malformed: return PipelineStatus::Malformed;
    case Result::Unsupported: return PipelineStatus::Unsupported;
    case Result::Valid: break;
    }

    // Single bounded walk; inspect() has already validated every envelope.
    const size_t start = 24 + ((input.bytes[1] & 0x80) ? 4 : 0) + 16;
    const uint8_t *p = input.bytes + start;
    size_t remaining = input.length - start;
    ElectionParameters legacy = {};
    bool haveLegacy = false, haveModern = false;
    while (remaining >= 3) {
        const size_t valueLength = syncRead16(p + 1);
        if (valueLength > remaining - 3) return PipelineStatus::Malformed;
        switch (p[0]) {
        case 4:
            out.sawSync = decodeSyncParameters(p + 3, valueLength, out.sync) || out.sawSync;
            break;
        case 5:
            haveLegacy = decodeElectionV1(p + 3, valueLength, legacy) || haveLegacy;
            break;
        case 24:
            haveModern = decodeElectionV2(p + 3, valueLength, out.election) || haveModern;
            break;
        case 18: {
            size_t consumed = 0;
            out.sawSequence = decodeChannelSequence(p + 3, valueLength, out.sequence, consumed) ==
                                  ChannelDecode::Valid || out.sawSequence;
            break;
        }
        default:
            break;
        }
        p += 3 + valueLength;
        remaining -= 3 + valueLength;
    }
    // A sender that speaks both is taken at its modern values, matching the
    // decoder's own rule.
    if (haveLegacy && !haveModern) out.election = legacy;
    out.sawElection = haveLegacy || haveModern;

    // Expire first: compaction would move the entry that is about to be updated.
    out.expiredPeers = peerTableAge(table, input.receiveGp2, kPeerTimeoutUs);
    Peer *peer = peerTableTouch(table, input.bytes + 10, input.receiveGp2);
    if (!peer) return PipelineStatus::TableFull;
    out.peer = peer;
    out.peerCount = table.count;
    if (out.sawElection) {
        peer->election = out.election;
        peer->hasElection = true;
        peer->sawV2 = peer->sawV2 || haveModern;
    }
    if (out.sawSequence) {
        peer->channels = out.sequence;
        peer->hasChannels = true;
    }

    if (!out.sawSync) return PipelineStatus::NoSync;
    out.voted = electSyncMaster(table, selfAddress);
    // An observation older than the peer timeout is not worth scheduling against.
    if (!predictWindow(out.sync, input.receiveGp2, input.receiveGp2, kPeerTimeoutUs, out.window))
        return PipelineStatus::SyncUnusable;
    if (!out.sawSequence) return PipelineStatus::NoSequence;
    out.scheduleConsistent = scheduleConsistentWithSync(out.sync, out.sequence);
    if (!predictNextSlot(out.sync, out.sequence, out.window, out.nextSlot))
        return PipelineStatus::NoSlot;
    return PipelineStatus::Ready;
}
} // namespace ItlAwdl
