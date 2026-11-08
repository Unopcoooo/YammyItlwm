// SPDX-License-Identifier: GPL-2.0-or-later
// Wire decoder only: no election, no radio operations, no peer state.
// Layout references: OWL src/frame.h struct awdl_election_params_tlv and
// awdl_election_params_v2_tlv, plus src/rx.c field offsets for TLV 5 and TLV 24.
// Independent implementation; no OWL code is copied or linked.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "SyncParameters.hpp"

namespace ItlAwdl {

// v2 senders also scatter the fields of this structure across the TLV; only the
// ones an election actually compares are kept, plus the self values for logging.
struct ElectionParameters {
    uint8_t masterAddress[6];
    uint8_t syncAddress[6]; // v2 only; zero for v1-only senders
    uint32_t masterCounter; // v2 only; zero for v1-only senders
    uint32_t masterMetric;
    uint32_t selfMetric;
    uint32_t selfCounter;   // v2 only
    uint32_t height;        // distance to master; v1 carries 8 bits, v2 carries 32
    bool v2;
};

inline uint32_t electionRead32(const uint8_t *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

// Input is TLV type 5's VALUE, excluding the 3-byte type/length envelope.
// Flags, the 16-bit id and the trailing pad are deliberately not retained.
inline bool decodeElectionV1(const uint8_t *p, size_t n, ElectionParameters &out) {
    out = {};
    if (!p || n < 19) return false;
    out.height = p[3];
    for (unsigned i = 0; i < 6; ++i) out.masterAddress[i] = p[5 + i];
    out.masterMetric = electionRead32(p + 11);
    out.selfMetric = electionRead32(p + 15);
    out.v2 = false;
    return true;
}

// Input is TLV type 24's VALUE, excluding the 3-byte type/length envelope.
// The unknown/reserved words at 28 and 32 are deliberately not retained.
inline bool decodeElectionV2(const uint8_t *p, size_t n, ElectionParameters &out) {
    out = {};
    if (!p || n < 40) return false;
    for (unsigned i = 0; i < 6; ++i) {
        out.masterAddress[i] = p[i];
        out.syncAddress[i] = p[6 + i];
    }
    out.masterCounter = electionRead32(p + 12);
    out.height = electionRead32(p + 16);
    out.masterMetric = electionRead32(p + 20);
    out.selfMetric = electionRead32(p + 24);
    out.selfCounter = electionRead32(p + 36);
    out.v2 = true;
    return true;
}

enum class ElectionExtraction { InvalidEnvelope, None, Malformed, Found };

// A true result certifies the wire layout of one advertisement, nothing about
// whether the values describe a sane sync tree. v2 wins over v1 in the same
// frame regardless of TLV order, matching the reference behaviour of ignoring
// v1 advertisement from senders that speak v2.
inline ElectionExtraction extractElectionParameters(const uint8_t *frame, size_t length,
                                                    ElectionParameters &out) {
    out = {};
    Summary envelope;
    if (inspect(frame, length, envelope) != Result::Valid) return ElectionExtraction::InvalidEnvelope;
    const size_t start = 24 + ((frame[1] & 0x80) ? 4 : 0) + 16;
    const uint8_t *p = frame + start;
    size_t remaining = length - start;
    bool foundV1 = false, foundV2 = false;
    ElectionParameters candidateV1 = {}, candidateV2 = {};
    // inspect() has already validated every TLV envelope in this complete frame.
    while (remaining) {
        const size_t valueLength = syncRead16(p + 1);
        if (p[0] == 24) {
            if (foundV2 || !decodeElectionV2(p + 3, valueLength, candidateV2)) return ElectionExtraction::Malformed;
            foundV2 = true;
        }
        if (p[0] == 5) {
            if (foundV1 || !decodeElectionV1(p + 3, valueLength, candidateV1)) return ElectionExtraction::Malformed;
            foundV1 = true;
        }
        p += 3 + valueLength;
        remaining -= 3 + valueLength;
    }
    if (foundV1 || foundV2) {
        out = foundV2 ? candidateV2 : candidateV1;
        return ElectionExtraction::Found;
    }
    return ElectionExtraction::None;
}
} // namespace ItlAwdl
