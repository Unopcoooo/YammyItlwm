#include "../../itlwm/awdl/ChannelSchedule.hpp"
#include <assert.h>
#include <initializer_list>
#include <stdio.h>
using namespace ItlAwdl;

// Values taken verbatim from the first probe6 frame: a real AWDL sync frame with
// all four TLVs, received 2026-10-15 after the probe6 reboot.
static const uint8_t kRealChannels[16] = {
    153, 153, 149, 0, 0, 0, 0, 153, 6, 153, 149, 153, 0, 0, 0, 153
};
static const uint8_t kRealAux[16] = {
    128, 128, 128, 0, 0, 0, 0, 128, 81, 128, 128, 128, 0, 0, 0, 128
};

static ChannelSequence realSequence() {
    ChannelSequence s = {};
    s.count = 16;
    s.steps = 4;
    s.encoding = 3;
    s.duplicates = 0;
    s.fill = 0xffff;
    for (unsigned i = 0; i < 16; ++i) {
        s.entries[i].channel = kRealChannels[i];
        s.entries[i].auxiliary = kRealAux[i];
    }
    return s;
}

static SyncParameters realSync() {
    SyncParameters sync = {};
    sync.awPeriod = 16;
    sync.afPeriod = 110;
    sync.presenceMode = 4;
    sync.guardTime = 0;
    sync.nextChannel = 6;
    sync.masterChannel = 6;
    sync.nextAwSequence = 35405;
    sync.txDownCounter = 41;
    sync.minExtension = 3;
    sync.maxMulticastExtension = 3;
    sync.maxUnicastExtension = 3;
    sync.maxActionExtension = 3;
    return sync;
}

int main() {
    const ChannelSequence sequence = realSequence();
    const SyncParameters sync = realSync();

    // The captured frame satisfies every agreement the reference implementation
    // demands of a peer, which is the first real-frame check of the sync fields
    // beyond their plain wire layout.
    assert(scheduleConsistentWithSync(sync, sequence));
    assert(sync.minExtension == sync.presenceMode - 1);
    assert(sync.maxMulticastExtension == sync.presenceMode - 1);
    assert(sync.maxUnicastExtension == sync.presenceMode - 1);
    assert(sync.maxActionExtension == sync.presenceMode - 1);
    assert(uint32_t(sync.awPeriod) * sync.presenceMode == 64); // 64 TU = 65.536 ms

    // Agreements that must break the check one at a time.
    ChannelSequence broken = sequence;
    broken.count = 15;
    assert(!scheduleConsistentWithSync(sync, broken));
    broken = sequence;
    broken.duplicates = 1;
    assert(!scheduleConsistentWithSync(sync, broken));
    broken = sequence;
    broken.fill = 0x1234;
    assert(!scheduleConsistentWithSync(sync, broken));
    broken = sequence;
    broken.steps = 8;
    assert(!scheduleConsistentWithSync(sync, broken));
    SyncParameters noPresence = sync;
    noPresence.presenceMode = 0;
    assert(!scheduleConsistentWithSync(noPresence, sequence));

    // Slot mapping over the real table: every slot, both multicast slots, and the
    // six empty ones the sender advertises.
    SlotView slot;
    unsigned empty = 0, multicast = 0;
    for (uint32_t eaw = 0; eaw < 16; ++eaw) {
        assert(scheduleSlot(sequence, eaw, slot));
        assert(slot.eawIndex == eaw && slot.slot == eaw);
        assert(slot.channel == kRealChannels[eaw]);
        if (slot.channel == 0) ++empty;
        if (slot.multicast) ++multicast;
    }
    assert(empty == 7);
    assert(multicast == 2);
    assert(scheduleSlot(sequence, 0, slot) && slot.multicast && slot.channel == 153);
    assert(scheduleSlot(sequence, 10, slot) && slot.multicast && slot.channel == 149);
    assert(scheduleSlot(sequence, 8, slot) && !slot.multicast && slot.channel == 6);

    // Wrapping is by EAW index, not by wall clock.
    assert(scheduleSlot(sequence, 16, slot) && slot.slot == 0 && slot.channel == 153);
    assert(scheduleSlot(sequence, 40, slot) && slot.slot == 8 && slot.channel == 6);
    assert(scheduleSlot(sequence, 0xffffffffu, slot));

    // Refuse to invent slots that the advertised count does not cover.
    broken = sequence;
    broken.count = 0;
    assert(!scheduleSlot(broken, 3, slot));

    // EAW index from an AW counter, and the counter's presence-mode alignment.
    assert(eawIndexFromAwCounter(35405, 4) == 8851);
    assert(eawIndexFromAwCounter(64, 4) == 16);
    assert(eawIndexFromAwCounter(63, 4) == 15);
    assert(eawIndexFromAwCounter(1234, 0) == 0);

    // The composed prediction: the window model's next boundary lands in a slot
    // whose channel comes from the same real table.
    WindowPrediction window;
    assert(predictWindow(sync, 1000000, 1000000, 1000000, window));
    assert(window.nextBoundaryGp2 == 1000000 + 41 * 1024);
    assert(window.nextBoundarySequence == 35408);
    assert(predictNextSlot(sync, sequence, window, slot));
    assert(slot.eawIndex == 8852 && slot.slot == 4 && slot.channel == 0);
    assert(predictNextSlot(sync, sequence, window, slot) && slot.channel == 0); // empty slot is still addressable

    SyncParameters noMode = sync;
    noMode.presenceMode = 0;
    assert(!predictNextSlot(noMode, sequence, window, slot));
    puts("channel schedule: real-frame agreements, slot mapping and window composition PASS (ASan/UBSan)");
}
