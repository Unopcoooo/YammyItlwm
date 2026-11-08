// SPDX-License-Identifier: GPL-2.0-or-later
// Peer bookkeeping and sync-tree election over already-decoded advertisements.
// Behaviour reference: OWL src/election.c (election rules) and src/peers.c
// (peer timeout). Independent re-implementation; no OWL code is copied or linked.
// No radio I/O, no allocation, no timers: the caller supplies every timestamp,
// and nothing here schedules a channel, transmits, or creates an interface.
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef KERNEL
#include <libkern/libkern.h>
#else
#include <string.h>
#endif
#include "ChannelSequence.hpp"
#include "ElectionParameters.hpp"
#include "TimingAudit.hpp"

namespace ItlAwdl {

constexpr unsigned kMaxPeers = 16;
constexpr uint32_t kPeerTimeoutUs = 2000000;  // reference default, 2 s
constexpr uint32_t kElectionMetricInit = 60;  // our own starting metric
constexpr uint32_t kElectionCounterInit = 0;
constexpr uint32_t kElectionTreeMaxHeight = 10;

struct Peer {
    uint8_t address[6];
    uint32_t lastSeenGp2;
    ElectionParameters election;
    ChannelSequence channels;
    SyncParameters sync;
    uint32_t syncReceivedGp2;
    bool hasSync;
    bool hasElection;
    bool hasChannels;
    bool sawV2;
    TimingAudit timing;
    uint32_t diagnosticId=0;
};

struct PeerTable {
    Peer peers[kMaxPeers];
    unsigned count;
};

inline bool peerSameAddress(const uint8_t *a, const uint8_t *b) {
    return memcmp(a, b, 6) == 0;
}

inline void peerTableReset(PeerTable &table) {
    table.count = 0;
}

inline Peer *peerTableFind(PeerTable &table, const uint8_t *address) {
    for (unsigned i = 0; i < table.count; ++i)
        if (peerSameAddress(table.peers[i].address, address)) return &table.peers[i];
    return nullptr;
}

// Find or insert. Never evicts a live peer: a full table is reported so the
// caller can expire entries first instead of silently dropping the new one.
inline Peer *peerTableTouch(PeerTable &table, const uint8_t *address, uint32_t nowGp2) {
    Peer *peer = peerTableFind(table, address);
    if (!peer) {
        if (table.count >= kMaxPeers) return nullptr;
        peer = &table.peers[table.count++];
        *peer = Peer();
        memcpy(peer->address, address, 6);
    }
    peer->lastSeenGp2 = nowGp2;
    return peer;
}

// Expire peers not seen within the timeout and compact the table.
// Age is computed as an unsigned difference, so the caller must keep the
// timeout below 2^31 microseconds and must not call this across a device reset.
inline unsigned peerTableAge(PeerTable &table, uint32_t nowGp2, uint32_t timeoutUs) {
    unsigned removed = 0;
    for (unsigned i = 0; i < table.count;) {
        if (nowGp2 - table.peers[i].lastSeenGp2 > timeoutUs) {
            table.peers[i] = table.peers[--table.count];
            ++removed;
        } else {
            ++i;
        }
    }
    return removed;
}

struct ElectionView {
    uint8_t masterAddress[6];
    uint8_t syncAddress[6];
    uint8_t selfAddress[6]; // address of the tree member we sync to, for tie-breaks
    uint32_t masterMetric;
    uint32_t masterCounter;
    uint32_t height;
    bool isSelfMaster;
};

inline int electionComparePair(uint32_t counterA, uint32_t metricA,
                               uint32_t counterB, uint32_t metricB) {
    if (counterA != counterB) return counterA < counterB ? -1 : 1;
    if (metricA != metricB) return metricA < metricB ? -1 : 1;
    return 0;
}

// Applies the documented election rules to the current table:
// highest master counter, then highest master metric, wins; on a tie the
// shorter distance to the master wins, then the larger member address.
// Entries that would push the tree past its height limit, and entries that
// would create a cycle back to us, are skipped. Without any usable
// advertisement we stay our own master.
inline ElectionView electSyncMaster(const PeerTable &table, const uint8_t *selfAddress) {
    ElectionView best = {};
    memcpy(best.selfAddress, selfAddress, 6);
    memcpy(best.masterAddress, selfAddress, 6);
    memcpy(best.syncAddress, selfAddress, 6);
    best.masterMetric = kElectionMetricInit;
    best.masterCounter = kElectionCounterInit;
    best.height = 0;
    best.isSelfMaster = true;
    uint32_t selectedAdvertisedHeight = 0;

    for (unsigned i = 0; i < table.count; ++i) {
        const Peer &peer = table.peers[i];
        if (!peer.hasElection) continue;
        if (uint64_t(peer.election.height) + 1 > kElectionTreeMaxHeight) continue;
        // A v1-only sender advertises no sync address, so a cycle through it
        // cannot be detected here and is reported as an unverified assumption.
        if (peer.election.v2 && peerSameAddress(peer.election.syncAddress, selfAddress)) continue;
        const int cmp = electionComparePair(peer.election.masterCounter, peer.election.masterMetric,
                                            best.masterCounter, best.masterMetric);
        if (cmp < 0) continue;
        if (cmp == 0) {
            if (peer.election.height > selectedAdvertisedHeight) continue;
            if (peer.election.height == selectedAdvertisedHeight &&
                memcmp(peer.address, best.selfAddress, 6) <= 0)
                continue;
        }
        memcpy(best.masterAddress, peer.election.masterAddress, 6);
        memcpy(best.syncAddress, peer.address, 6);
        memcpy(best.selfAddress, peer.address, 6);
        best.masterMetric = peer.election.masterMetric;
        best.masterCounter = peer.election.masterCounter;
        best.height = peer.election.height + 1;
        selectedAdvertisedHeight = peer.election.height;
        best.isSelfMaster = false;
    }
    return best;
}
} // namespace ItlAwdl
