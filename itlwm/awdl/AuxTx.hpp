// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "ActionFrame.hpp"
namespace ItlAwdl {
enum class AuxTxPhase { Ready, Submitted, Completed, FlushRequested, Quarantined, Stopped, Skipped };
struct AuxTxOwnership {
    AuxTxPhase phase = AuxTxPhase::Ready;
    bool attempted = false, pending = false, flushSent = false;
    uint32_t generation = 0;
    unsigned queue = 0, index = 0;
    uint64_t deadlineNs = 0;
    bool attempt() { if (attempted) return false; attempted = true; return true; }
    bool submit(uint32_t epoch, unsigned q, unsigned slot, uint64_t deadline) {
        if (!attempted || pending || phase != AuxTxPhase::Ready) return false;
        generation = epoch; queue = q; index = slot; deadlineNs = deadline;
        phase = AuxTxPhase::Submitted; pending = true; return true;
    }
    bool matches(uint32_t epoch, unsigned q, unsigned slot) const {
        return pending && epoch == generation && queue == q && index == slot;
    }
    bool complete(uint32_t epoch, unsigned q, unsigned slot) {
        if (!matches(epoch,q,slot)) return false;
        pending = false; phase = AuxTxPhase::Completed; return true;
    }
    bool requestFlush(uint32_t epoch, uint64_t deadline) {
        if (!pending || flushSent || epoch != generation) return false;
        flushSent = true; phase = AuxTxPhase::FlushRequested; deadlineNs = deadline; return true;
    }
    void quarantine() { phase = AuxTxPhase::Quarantined; } // retain ownership until completion/reset
    void skipped() {
        if (pending) return;
        attempted = flushSent = false;
        generation = queue = index = 0;
        deadlineNs = 0; phase = AuxTxPhase::Ready;
    }
    void deviceQuiesced() { pending = false; phase = attempted ? AuxTxPhase::Stopped : AuxTxPhase::Ready; }
    bool recycle() {
        if (pending || phase != AuxTxPhase::Completed) return false;
        attempted = false; flushSent = false; generation = 0; queue = 0; index = 0;
        deadlineNs = 0; phase = AuxTxPhase::Ready; return true;
    }
};
inline bool auxTxWindowOpen(uint32_t epoch, uint32_t eventEpoch, uint32_t nowGp2,
                             uint32_t startGp2, bool running) {
    return running && epoch == eventEpoch && uint32_t(nowGp2 - startGp2) <= 2000;
}
inline uint16_t auxUnencryptedSchedulerEntry(uint16_t length, uint8_t station) {
    return uint16_t(((unsigned(length) + 8 + 3) / 4) | (unsigned(station) << 12));
}
// Envelope-only TX transport probe. No sync/election/service claims, no user
// data. A valid envelope is NOT a complete AWDL advertisement or interoperability.
inline bool buildAuxTxProbe(const uint8_t source[6], uint32_t gp2, uint8_t out[40]) {
    if (!source || !out || (source[0] & 1)) return false;
    uint8_t any = 0; for (unsigned i=0;i<6;++i) any |= source[i];
    if (!any) return false;
    for (unsigned i=0;i<40;++i) out[i]=0;
    out[0]=0xd0;
    const uint8_t bssid[6]={0,0x25,0,0xff,0x94,0x73};
    for (unsigned i=0;i<6;++i) { out[4+i]=255; out[10+i]=source[i]; out[16+i]=bssid[i]; }
    out[24]=127; out[26]=0x17; out[27]=0xf2; out[28]=8; out[29]=0x10;
    for (unsigned i=0;i<4;++i) out[32+i]=out[36+i]=uint8_t(gp2>>(8*i));
    return true;
}
}
