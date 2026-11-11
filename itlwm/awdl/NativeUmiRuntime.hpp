// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "NativeUmi.hpp"

struct NativeUmiFlight {
    ItlIwm *owner = nullptr;
    bool pending = false, flight = false;
    uint8_t peer[6] = {};
    uint16_t length = 0;
    uint8_t payload[ItlAwdl::kNativeUmiPayloadMax] = {};
    uint64_t deadlineNs = 0;
    uint32_t entries = 0, queued = 0, submitted = 0, completed = 0, failed = 0, dropped = 0;
    uint32_t lastReason = 0;
    uint8_t lastKind = 0;
};

static NativeUmiFlight gItlNativeUmi;

void ItlIwm::awdlNativeUmiPublish() {
    auto *c = getController();
    if (!c) return;
    c->setProperty("AWDLNativeUmiQueued", gItlNativeUmi.queued, 32);
    c->setProperty("AWDLNativeUmiSubmitted", gItlNativeUmi.submitted, 32);
    c->setProperty("AWDLNativeUmiCompleted", gItlNativeUmi.completed, 32);
    c->setProperty("AWDLNativeUmiFailed", gItlNativeUmi.failed, 32);
    c->setProperty("AWDLNativeUmiDropped", gItlNativeUmi.dropped, 32);
    c->setProperty("AWDLNativeUmiEntries", gItlNativeUmi.entries, 32);
    c->setProperty("AWDLNativeUmiLastKind", gItlNativeUmi.lastKind, 8);
    c->setProperty("AWDLNativeUmiLastReason", gItlNativeUmi.lastReason, 32);
    c->setProperty("AWDLNativeUmiPending", gItlNativeUmi.pending);
    c->setProperty("AWDLNativeUmiFlight", gItlNativeUmi.flight);
    if (gItlNativeUmi.length)
        c->setProperty("AWDLNativeUmiPeer", gItlNativeUmi.peer, 6);
}

static void itlNativeUmiQuiesce(ItlIwm *hal) {
    if (!hal || gItlNativeUmi.owner != hal) return;
    if (gItlNativeUmi.pending || gItlNativeUmi.flight) ++gItlNativeUmi.dropped;
    gItlNativeUmi.pending = gItlNativeUmi.flight = false;
    gItlNativeUmi.length = 0;
    gItlNativeUmi.deadlineNs = 0;
    hal->awdlNativeUmiPublish();
}

IOReturn ItlIwm::awdlNativeUmiRequest(const uint8_t peer[6], const uint8_t *payload,
                                     size_t length, uint8_t kind) {
    ++gItlNativeUmi.entries;
    gItlNativeUmi.lastKind = kind;
    auto reject = [this](uint32_t reason) {
        gItlNativeUmi.lastReason = reason;
        awdlNativeUmiPublish();
        return reason == 7 ? kIOReturnBusy : kIOReturnNotReady;
    };
    if (!getMainWorkLoop() || !getMainWorkLoop()->inGate()) return reject(1);
    if (dmaDetaching) return reject(2);
    const bool discovery = awdlDiscoveryEnabled && awdlNativeDiscovery.open &&
        !awdlNativeDiscovery.closed;
    if (!awdlAuxTxEnabled && !discovery) return reject(3);
    if (!ItlAwdl::nativeUmiUnicast(peer)) return reject(4);
    if (!ItlAwdl::nativeUmiPayload(payload, length)) return reject(5);
    if (!awdlNativeSyncRoleConfirmed()) return reject(6);
    if (gItlNativeUmi.pending || gItlNativeUmi.flight) return reject(7);
    gItlNativeUmi.owner = this;
    gItlNativeUmi.pending = true;
    gItlNativeUmi.flight = false;
    memcpy(gItlNativeUmi.peer, peer, 6);
    memcpy(gItlNativeUmi.payload, payload, length);
    gItlNativeUmi.length = uint16_t(length);
    uint64_t uptime = 0, now = 0;
    clock_get_uptime(&uptime);
    absolutetime_to_nanoseconds(uptime, &now);
    gItlNativeUmi.deadlineNs = now + 5000000000ULL;
    ++gItlNativeUmi.queued;
    gItlNativeUmi.lastReason = 8;
    awdlNativeUmiPublish();
    // The native caller runs in the command gate. Try the current AW now;
    // if the AUX slot is busy, the existing timer retries from flush/tick.
    awdlAuxTxStart();
    if (gItlNativeUmi.pending)
        getMainCommandGate()->commandWakeup(&awdlIbssWorkPending, false);
    return kIOReturnSuccess;
}
