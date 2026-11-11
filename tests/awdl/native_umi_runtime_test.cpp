#include "NativeUmi.hpp"
#include <assert.h>
#include <stdint.h>
#include <string.h>

using IOReturn = int;
constexpr IOReturn kIOReturnSuccess = 0;
constexpr IOReturn kIOReturnNotReady = 0xe00002bc;
constexpr IOReturn kIOReturnBusy = 0xe00002d5;

static uint64_t gUptime = 100;
static void clock_get_uptime(uint64_t *value) { *value = gUptime; }
static void absolutetime_to_nanoseconds(uint64_t value, uint64_t *ns) { *ns = value * 1000; }

struct FakeController {
    uint32_t entries = 0;
    uint32_t reason = 0;
    void setProperty(const char *name, uint32_t value, int) {
        if (!strcmp(name, "AWDLNativeUmiEntries")) entries = value;
        if (!strcmp(name, "AWDLNativeUmiLastReason")) reason = value;
    }
    void setProperty(const char *, bool) {}
    void setProperty(const char *, const uint8_t *, size_t) {}
};
struct FakeGate {
    unsigned wakes = 0;
    void commandWakeup(bool *, bool) { ++wakes; }
};
struct FakeLoop {
    bool gated = true;
    bool inGate() const { return gated; }
};
struct FakeDiscovery {
    bool open = false, closed = false;
};

static bool awdlIbssWorkPending = false;

class ItlIwm {
public:
    bool dmaDetaching = false;
    bool awdlDiscoveryEnabled = false;
    bool awdlAuxTxEnabled = false;
    FakeDiscovery awdlNativeDiscovery;
    FakeLoop loop;
    FakeGate gate;
    FakeController controller;
    unsigned starts = 0;
    bool roleConfirmed = true;

    FakeLoop *getMainWorkLoop() { return &loop; }
    FakeGate *getMainCommandGate() { return &gate; }
    FakeController *getController() { return &controller; }
    void awdlAuxTxStart() { ++starts; }
    bool awdlNativeSyncRoleConfirmed() const { return roleConfirmed; }
    void awdlNativeUmiPublish();
    IOReturn awdlNativeUmiRequest(const uint8_t *, const uint8_t *, size_t, uint8_t);
};

#include "NativeUmiRuntime.hpp"

static void reset(ItlIwm &hal) {
    gItlNativeUmi = NativeUmiFlight{};
    hal = ItlIwm{};
}

int main() {
    const uint8_t peer[6] = {0xb6, 0xa8, 0xe1, 0xb4, 0x89, 0x41};
    const uint8_t group[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    const uint8_t payload[8] = {4, 0x49, 0, 6, 0x2c, 0, 6, 0};
    ItlIwm hal;

    reset(hal);
    hal.loop.gated = false;
    assert(hal.awdlNativeUmiRequest(peer, payload, sizeof(payload), 1) == kIOReturnNotReady);
    assert(gItlNativeUmi.lastReason == 1);

    reset(hal);
    assert(hal.awdlNativeUmiRequest(peer, payload, sizeof(payload), 1) == kIOReturnNotReady);
    assert(gItlNativeUmi.lastReason == 3);
    assert(hal.starts == 0);

    reset(hal);
    hal.awdlDiscoveryEnabled = true;
    hal.awdlNativeDiscovery.open = true;
    assert(hal.awdlNativeUmiRequest(peer, payload, sizeof(payload), 2) == kIOReturnSuccess);
    assert(gItlNativeUmi.entries == 1 && gItlNativeUmi.queued == 1);
    assert(gItlNativeUmi.lastKind == 2 && gItlNativeUmi.lastReason == 8);
    assert(gItlNativeUmi.pending && !gItlNativeUmi.flight && hal.starts == 1);
    assert(hal.controller.entries == 1 && hal.controller.reason == 8);

    assert(hal.awdlNativeUmiRequest(peer, payload, sizeof(payload), 1) == kIOReturnBusy);
    assert(gItlNativeUmi.lastReason == 7);

    reset(hal);
    hal.awdlDiscoveryEnabled = true;
    hal.awdlNativeDiscovery.open = true;
    assert(hal.awdlNativeUmiRequest(group, payload, sizeof(payload), 2) == kIOReturnNotReady);
    assert(gItlNativeUmi.lastReason == 4);

    reset(hal);
    hal.awdlDiscoveryEnabled = true;
    hal.awdlNativeDiscovery.open = true;
    assert(hal.awdlNativeUmiRequest(peer, payload, 0, 2) == kIOReturnNotReady);
    assert(gItlNativeUmi.lastReason == 5);

    reset(hal);
    hal.awdlDiscoveryEnabled = true;
    hal.awdlNativeDiscovery.open = true;
    hal.roleConfirmed = false;
    assert(hal.awdlNativeUmiRequest(peer, payload, sizeof(payload), 1) == kIOReturnNotReady);
    assert(gItlNativeUmi.lastReason == 6);

    reset(hal);
    hal.awdlDiscoveryEnabled = true;
    hal.awdlNativeDiscovery.open = true;
    assert(hal.awdlNativeUmiRequest(peer, payload, sizeof(payload), 1) == kIOReturnSuccess);
    itlNativeUmiQuiesce(&hal);
    assert(!gItlNativeUmi.pending && !gItlNativeUmi.flight && gItlNativeUmi.dropped == 1);
    return 0;
}
