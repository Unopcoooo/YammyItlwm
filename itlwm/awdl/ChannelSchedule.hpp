// SPDX-License-Identifier: GPL-2.0-or-later
// Slot arithmetic over already-decoded AWDL parameters.
// Behaviour reference: OWL src/schedule.c (the slot is the current EAW index
// modulo the channel-sequence length) and src/sync.c (EAW period is
// presence_mode * aw_period, and the AW counter advances by presence_mode per
// EAW). Independent re-implementation; no OWL code is copied or linked.
// Offline arithmetic only: no radio I/O, no timers, no channel changes.
#pragma once
#include "ChannelSequence.hpp"
#include "SyncParameters.hpp"
#include "WindowModel.hpp"

namespace ItlAwdl {

// The reference implementation hardcodes these two slots as the multicast EAWs;
// this is a convention, not something the wire describes.
constexpr uint32_t kMulticastSlotFirst = 0;
constexpr uint32_t kMulticastSlotSecond = 10;

struct SlotView {
    uint32_t eawIndex;
    uint8_t slot;
    uint8_t channel; // 0 means the slot names no channel
    bool multicast;
};

// A slot is only usable when the sequence actually covers it; entries beyond the
// advertised count are not invented here.
inline bool scheduleSlot(const ChannelSequence &sequence, uint32_t eawIndex, SlotView &out) {
    out = {};
    if (!sequence.count || sequence.count > kChannelSequenceSlots) return false;
    const uint32_t slot = eawIndex % kChannelSequenceSlots;
    if (slot >= sequence.count) return false;
    out.eawIndex = eawIndex;
    out.slot = uint8_t(slot);
    out.channel = sequence.entries[slot].channel;
    out.multicast = slot == kMulticastSlotFirst || slot == kMulticastSlotSecond;
    return true;
}

// The reference implementation rejects a peer whose channel sequence disagrees
// with its own presence mode, so the same agreement is checked here before any
// schedule arithmetic is trusted.
inline bool scheduleConsistentWithSync(const SyncParameters &sync, const ChannelSequence &sequence) {
    if (sequence.count != kChannelSequenceSlots) return false;
    if (sequence.duplicates != 0) return false;
    if (sequence.fill != 0xffff) return false;
    if (!sync.presenceMode) return false;
    return uint32_t(sequence.steps) == uint32_t(sync.presenceMode);
}

// The AW counter advances by presence_mode per EAW, so the EAW index is that
// counter divided by it. The reference masks the low bits off on every update,
// which is only valid while the counter keeps that alignment.
inline uint32_t eawIndexFromAwCounter(uint32_t awCounter, uint8_t presenceMode) {
    if (!presenceMode) return 0;
    return awCounter / presenceMode;
}

// Composes the window prediction with the channel table: which slot the next
// availability window falls in, and which channel that slot names.
inline bool predictNextSlot(const SyncParameters &sync, const ChannelSequence &sequence,
                            const WindowPrediction &window, SlotView &out) {
    out = {};
    if (!scheduleConsistentWithSync(sync, sequence)) return false;
    // A 16-bit AW counter must wrap on a complete channel-sequence cycle.
    // Other presence modes need an extended counter rather than this model.
    if (65536u % (uint32_t(sync.presenceMode) * kChannelSequenceSlots)) return false;
    if (window.nextBoundarySequence % sync.presenceMode) return false;
    return scheduleSlot(sequence, eawIndexFromAwCounter(window.nextBoundarySequence, sync.presenceMode), out);
}
} // namespace ItlAwdl
