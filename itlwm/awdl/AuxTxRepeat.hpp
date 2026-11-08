// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "AuxTx.hpp"
namespace ItlAwdl {
// Offline preparation for M2; deliberately not included by the live adapter.
// Single workloop writer. Caller must prove the AUX ring is idle before begin.
// This policy never frees DMA and does not establish over-the-air delivery.
class AuxTxRepeat {
public:
    AuxTxRepeat() : tx_(owned_) {}
    explicit AuxTxRepeat(AuxTxOwnership &transport) : tx_(transport) {}
    AuxTxRepeat(const AuxTxRepeat &) = delete;
    AuxTxRepeat &operator=(const AuxTxRepeat &) = delete;
    enum class Stop { NotArmed, Active, Budget, Deadline, Generation, Clock,
                      Cancelled, Quarantined, DeviceStopped };
    static constexpr unsigned MaxAttempts = 24, RingSlots = 256;
    static constexpr uint64_t MaxDurationNs = 600000000000ULL;
    bool arm(uint32_t generation, unsigned queue, uint64_t now,
             uint64_t deadline, unsigned budget) {
        if (reason_ != Stop::NotArmed || !budget || budget > MaxAttempts ||
            queue >= 32 || deadline <= now || deadline - now > MaxDurationNs) return false;
        generation_ = generation; queue_ = queue; lastNs_ = now;
        deadline_ = deadline; budget_ = budget; reason_ = Stop::Active;
        return true;
    }
    bool begin(uint32_t generation, uint64_t now, uint32_t window,
               unsigned queue, unsigned slot, bool queueIdle) {
        if (!live(generation, now) || reserved_ || tx_.pending || !queueIdle ||
            queue != queue_ || slot >= RingSlots || !window || window <= lastWindow_) return false;
        // Never reuse a published ring slot in this experiment/generation. Firmware
        // completion carries no policy ticket; a local serial cannot fix ABA.
        if (used_[slot / 32] & (uint32_t(1) << (slot % 32))) return false;
        if (tx_.attempted && tx_.phase != AuxTxPhase::Completed && tx_.phase != AuxTxPhase::Skipped) return false;
        tx_ = {}; tx_.attempt(); reserved_ = true; slot_ = slot;
        lastWindow_ = window; ++attempts_;
        return true;
    }
    bool submit(uint32_t generation, uint64_t now, uint64_t completionDeadline) {
        // Reserved attempts count even if preparation is skipped. Expiry can
        // reject publication; any caller-owned unsubmitted buffer stays theirs.
        if (!reserved_ || !current(generation, now) || completionDeadline <= now ||
            completionDeadline > deadline_) return false;
        if (!tx_.submit(generation_, queue_, slot_, completionDeadline)) return false;
        // Reserve a slot against ABA only once it can be published. A failed
        // unsubmitted preparation has no firmware completion to arrive late.
        used_[slot_ / 32] |= uint32_t(1) << (slot_ % 32);
        reserved_ = false; return true;
    }
    bool skipped() {
        if (!reserved_ || tx_.pending) return false;
        tx_.skipped(); reserved_ = false; return true;
    }
    bool complete(uint32_t generation, unsigned queue, unsigned slot) {
        // Late completion can release ownership after stop/quarantine, but never
        // re-arms this policy. Adapter must still reclaim the actual ring entry.
        return tx_.complete(generation, queue, slot);
    }
    bool requestFlush(uint32_t generation, uint64_t cleanupDeadline) {
        return tx_.requestFlush(generation, cleanupDeadline);
    }
    void cancel() { stop(Stop::Cancelled); }
    void quarantine() {
        if (tx_.pending) tx_.quarantine();
        stop(Stop::Quarantined);
    }
    void deviceQuiesced() {
        tx_.deviceQuiesced(); reserved_ = false; stop(Stop::DeviceStopped);
    }
    bool poll(uint32_t generation, uint64_t now) { return live(generation, now); }
    const AuxTxOwnership &ownership() const { return tx_; }
    Stop reason() const { return reason_; }
    unsigned attempts() const { return attempts_; }
    bool reserved() const { return reserved_; }
private:
    void stop(Stop why) {
        if (reason_ == Stop::Active || reason_ == Stop::NotArmed) reason_ = why;
    }
    bool current(uint32_t generation, uint64_t now) {
        if (reason_ != Stop::Active) return false;
        if (generation != generation_) { stop(Stop::Generation); return false; }
        if (now < lastNs_) { stop(Stop::Clock); return false; }
        lastNs_ = now;
        if (now >= deadline_) { stop(Stop::Deadline); return false; }
        return true;
    }
    bool live(uint32_t generation, uint64_t now) {
        if (!current(generation, now)) return false;
        // The last reserved/pending attempt must still be permitted to submit
        // and drain. Budget stops the next admission, never fakes completion.
        if (attempts_ >= budget_ && !reserved_ && !tx_.pending) {
            stop(Stop::Budget); return false;
        }
        return true;
    }
    AuxTxOwnership owned_;
    AuxTxOwnership &tx_;
    Stop reason_ = Stop::NotArmed;
    uint32_t generation_ = 0, lastWindow_ = 0, used_[8] = {};
    unsigned queue_ = 0, slot_ = 0, attempts_ = 0, budget_ = 0;
    uint64_t deadline_ = 0, lastNs_ = 0;
    bool reserved_ = false;
};
}
