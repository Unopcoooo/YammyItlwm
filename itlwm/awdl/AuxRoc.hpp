// SPDX-License-Identifier: GPL-2.0-or-later
// Offline Intel legacy HOT_SPOT_CMD codec and conservative lifecycle model.
// Wire facts: Linux v5.4 iwlwifi fw/api/time-event.h, phy-ctxt.h.
// No firmware calls, queue access, timers or AWDL scheduling in this module.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "RxControl.hpp"
namespace ItlAwdl {
struct AuxRocCommand { uint8_t bytes[44]; size_t length; };
inline uint32_t rocRead32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline void rocWrite32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(value >> (i * 8));
}
// Conservative experimental subset: 20 MHz on social channels 6/44/149.
// Caller must additionally check regulatory flags, capability 18, aux station
// readiness, association/DTIM constraints and radio conflicts before sending.
// applyGp2 is CURRENT device time, not a future AWDL boundary. The API permits
// firmware postponement; actual START notification establishes residency.
inline bool buildAuxRocAdd(bool extendedChannels, uint8_t channel, uint8_t station,
                           const uint8_t node[6], uint32_t applyGp2,
                           uint32_t durationTu, uint32_t maxDelayTu, AuxRocCommand &out,
                           uint8_t receiveControlHome = 0) {
    out = {};
    if (!node || (node[0] & 1) || station > 15 || !durationTu || durationTu > 64 || maxDelayTu > 600)
        return false;
    uint8_t nonzero = 0;
    for (unsigned i = 0; i < 6; ++i) nonzero |= node[i];
    const bool social=channel==6 || channel==44 || channel==149;
    const bool homeControl=receiveControlHome==channel && rxControlHomeAllowed(channel);
    if (!nonzero || (!social && !homeControl)) return false;
    AuxRocCommand cmd = {};
    rocWrite32(cmd.bytes, 4); // MAC_INDEX_AUX, color 0
    rocWrite32(cmd.bytes + 4, 1); // FW_CTXT_ACTION_ADD
    rocWrite32(cmd.bytes + 12, station);
    const size_t tail = extendedChannels ? 24 : 20;
    if (extendedChannels) {
        rocWrite32(cmd.bytes + 16, channel);
        cmd.bytes[20] = channel <= 14 ? 1 : 0;
    } else {
        cmd.bytes[16] = channel <= 14 ? 1 : 0;
        cmd.bytes[17] = channel;
    }
    // width=20 MHz and ctrl_pos=0, including reserved bytes, already zero.
    for (unsigned i = 0; i < 6; ++i) cmd.bytes[tail + i] = node[i];
    rocWrite32(cmd.bytes + tail + 8, applyGp2);
    rocWrite32(cmd.bytes + tail + 12, maxDelayTu);
    rocWrite32(cmd.bytes + tail + 16, durationTu);
    cmd.length = tail + 20;
    out = cmd;
    return true;
}
inline AuxRocCommand buildAuxRocRemove(bool extendedChannels, uint32_t uid) {
    AuxRocCommand out = {};
    out.length = extendedChannels ? 44 : 40;
    rocWrite32(out.bytes, 4);
    rocWrite32(out.bytes + 4, 3); // REMOVE is 3, not MODIFY (2).
    rocWrite32(out.bytes + 8, uid);
    return out;
}
enum class RocPhase { Idle, Submitted, AwaitStart, Running, Stopping, Draining, Quarantined };
struct RocNotification { uint32_t gp2, uid, mac, action, status; };
inline bool decodeRocNotification(const uint8_t *p, size_t n, RocNotification &out) {
    out = {};
    if (!p || n != 24) return false;
    out.gp2 = rocRead32(p); out.uid = rocRead32(p + 8);
    out.mac = rocRead32(p + 12); out.action = rocRead32(p + 16); out.status = rocRead32(p + 20);
    return true;
}
// Lifecycle policy written independently from the driver: never reuse an event
// after an ambiguous failure. Caller owns serialization, command deadlines,
// generation validation, RX dispatch and queue draining. UID alone is not proof
// of residency. This is not wired into the live driver.
struct AuxRocSession {
    RocPhase phase = RocPhase::Idle;
    uint32_t generation = 0, uid = 0, startGp2 = 0;
    uint32_t startCount = 0, endCount = 0, failureCount = 0, endGp2 = 0, lastStatus = 0;
    RocNotification early[2] = {};
    unsigned earlyCount = 0;
    bool prepareNext(uint32_t epoch){
        if(epoch!=generation||phase!=RocPhase::Idle)return false;
        resetAfterDeviceQuiesced(epoch);return true;
    }
    void resetAfterDeviceQuiesced(uint32_t newGeneration) {
        *this = AuxRocSession(); generation = newGeneration;
    }
    bool begin(uint32_t epoch) {
        if (epoch != generation || phase != RocPhase::Idle) return false;
        phase = RocPhase::Submitted; earlyCount = 0; return true;
    }
    bool notify(const uint8_t *p, size_t n, uint32_t epoch) {
        if (epoch != generation) return false;
        RocNotification event;
        if (!decodeRocNotification(p, n, event) || event.mac != 4) return false;
        if (phase == RocPhase::Submitted) {
            if (earlyCount == 2) { phase = RocPhase::Quarantined; return false; }
            early[earlyCount++] = event; return true;
        }
        return apply(event);
    }
    bool apply(const RocNotification &event) {
        if (event.uid != uid || event.mac != 4 || phase == RocPhase::Idle ||
            phase == RocPhase::Submitted || phase == RocPhase::Quarantined || phase == RocPhase::Draining)
            return false;
        if (!event.status || (event.action & 2)) {
            lastStatus = event.status;
            if (!event.status) ++failureCount;
            if (event.action & 2) { ++endCount; endGp2 = event.gp2; }
            phase = RocPhase::Draining; return true;
        }
        if ((event.action & 1) && phase == RocPhase::AwaitStart) {
            lastStatus = event.status; ++startCount;
            startGp2 = event.gp2; phase = RocPhase::Running; return true;
        }
        return false;
    }
    bool response(const uint8_t *p, size_t n, uint32_t epoch, uint32_t retiredUid=0, bool retiredValid=false) {
        if (epoch != generation || phase != RocPhase::Submitted) return false;
        if (!p || n != 8) { phase = RocPhase::Quarantined; return false; }
        uid = rocRead32(p);
        if (rocRead32(p + 4) != 0) {
            // Only terminal duplicates of an already retired UID are unambiguous.
            bool retiredOnly=retiredValid;
            for(unsigned i=0;i<earlyCount;++i)
                retiredOnly=retiredOnly&&early[i].uid==retiredUid&&(early[i].action&2);
            phase = earlyCount&&!retiredOnly ? RocPhase::Quarantined : RocPhase::Idle;
            earlyCount = 0; return false;
        }
        phase = RocPhase::AwaitStart;
        for (unsigned i = 0; i < earlyCount; ++i) apply(early[i]);
        earlyCount = 0; return true;
    }
    bool requestStop() {
        if (phase != RocPhase::AwaitStart && phase != RocPhase::Running) return false;
        phase = RocPhase::Stopping; return true;
    }
    void uncertainFailure() { if (phase != RocPhase::Idle) phase = RocPhase::Quarantined; }
    bool queueDrained() {
        if (phase != RocPhase::Draining) return false;
        phase = RocPhase::Idle; uid = startGp2 = 0; return true;
    }
    bool mayTransmit(uint32_t epoch) const { return epoch == generation && phase == RocPhase::Running; }
};
}
