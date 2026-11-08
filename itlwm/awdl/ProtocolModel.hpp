// SPDX-License-Identifier: GPL-2.0-or-later
// Offline composition of wire decoders, peer election and window arithmetic.
// Fixed storage; no OS dependencies, radio operations or interface publication.
#pragma once
#include "PeerTable.hpp"
#include "ChannelSchedule.hpp"
#include "AfTxTiming.hpp"
namespace ItlAwdl {
struct Advertisement {
    uint8_t sender[6];
    uint32_t txDelayUs=0;
    SyncParameters sync;
    ChannelSequence channels;
    ElectionParameters election;
};
enum class AdvertisementResult { Valid, Envelope, Incomplete, Malformed, Duplicate, Inconsistent, Sender };
// Strict experimental subset: one sync, one standalone channel sequence and
// election v1 and/or v2. v2 takes precedence independent of TLV order. Do not
// silently accept a malformed or duplicate known TLV behind a valid v2 TLV.
inline AdvertisementResult decodeAdvertisement(const uint8_t *frame, size_t length, Advertisement &out) {
    out = {};
    Summary summary;
    if (inspect(frame, length, summary) != Result::Valid) return AdvertisementResult::Envelope;
    Advertisement candidate = {};
    memcpy(candidate.sender, frame + 10, 6);
    const uint8_t zero[6] = {};
    if ((candidate.sender[0] & 1) || peerSameAddress(candidate.sender, zero))
        return AdvertisementResult::Sender;
    bool sync = false, channels = false, v1 = false, v2 = false;
    ElectionParameters first = {}, second = {};
    size_t offset = 24 + ((frame[1] & 0x80) ? 4 : 0) + 16;
    while (offset < length) {
        const uint8_t type = frame[offset];
        const size_t n = syncRead16(frame + offset + 1);
        const uint8_t *value = frame + offset + 3;
        if (type == 4) {
            if (sync) return AdvertisementResult::Duplicate;
            if (!decodeSyncParameters(value, n, candidate.sync)) return AdvertisementResult::Malformed;
            sync = true;
        } else if (type == 18) {
            if (channels) return AdvertisementResult::Duplicate;
            size_t consumed;
            if (decodeChannelSequence(value, n, candidate.channels, consumed) != ChannelDecode::Valid)
                return AdvertisementResult::Malformed;
            channels = true;
        } else if (type == 5) {
            if (v1) return AdvertisementResult::Duplicate;
            if (!decodeElectionV1(value, n, first)) return AdvertisementResult::Malformed;
            v1 = true;
        } else if (type == 24) {
            if (v2) return AdvertisementResult::Duplicate;
            if (!decodeElectionV2(value, n, second)) return AdvertisementResult::Malformed;
            v2 = true;
        }
        offset += n + 3;
    }
    if (!sync || !channels || (!v1 && !v2)) return AdvertisementResult::Incomplete;
    if (!scheduleConsistentWithSync(candidate.sync, candidate.channels)) return AdvertisementResult::Inconsistent;
    AfTxTiming wire;
    if(!decodeAfTxTiming(frame,length,wire))return AdvertisementResult::Malformed;
    candidate.txDelayUs=wire.phy-wire.target;
    const uint64_t periodUs=uint64_t(candidate.sync.awPeriod)*candidate.sync.presenceMode*1024;
    if(!periodUs || periodUs>=0x80000000ull || candidate.txDelayUs>periodUs)
        return AdvertisementResult::Malformed;
    candidate.election = v2 ? second : first;
    out = candidate;
    return AdvertisementResult::Valid;
}
enum class IngestResult { Accepted, Rejected, Self, Full, Downgrade };
inline IngestResult ingestAdvertisement(PeerTable &table, const uint8_t self[6],
                                       const uint8_t *frame, size_t length, uint32_t receivedGp2) {
    Advertisement advert;
    if (decodeAdvertisement(frame, length, advert) != AdvertisementResult::Valid) return IngestResult::Rejected;
    if (peerSameAddress(advert.sender, self)) return IngestResult::Self;
    peerTableAge(table, receivedGp2, kPeerTimeoutUs);
    Peer *existing = peerTableFind(table, advert.sender);
    // Keep a live v2 peer's state intact on a v1-only advertisement; expiry
    // permits a newly observed v1 peer. No partial field refresh on rejection.
    if (existing && existing->sawV2 && !advert.election.v2) return IngestResult::Downgrade;
    Peer *peer = peerTableTouch(table, advert.sender, receivedGp2);
    if (!peer) return IngestResult::Full;
    peer->timing.observe(advert.sync, advert.channels, receivedGp2,advert.txDelayUs);
    peer->election = advert.election; peer->channels = advert.channels;
    peer->sync = advert.sync; peer->syncReceivedGp2 = receivedGp2;
    peer->hasElection = peer->hasChannels = peer->hasSync = true;
    peer->sawV2 = advert.election.v2;
    return IngestResult::Accepted;
}
struct ProtocolPrediction {
    ElectionView election;
    WindowPrediction window;
    SlotView slot;
};
// Caller must reset the table on firmware reset. This is a prediction model,
// not authorization to transmit. Channel 0 is an empty slot, not a radio target.
inline bool predictProtocol(PeerTable &table, const uint8_t self[6], uint32_t nowGp2,
                            ProtocolPrediction &out) {
    out = {};
    peerTableAge(table, nowGp2, kPeerTimeoutUs);
    ProtocolPrediction result = {};
    result.election = electSyncMaster(table, self);
    if (result.election.isSelfMaster) return false;
    const Peer *peer = peerTableFind(table, result.election.syncAddress);
    if (!peer || !peer->hasSync || !peer->hasChannels) return false;
    if (!predictWindowWithTxDelay(peer->sync,peer->syncReceivedGp2,nowGp2,kPeerTimeoutUs,peer->timing.txDelayUs,result.window) ||
        !predictNextSlot(peer->sync, peer->channels, result.window, result.slot)) return false;
    out = result;
    return true;
}
}
