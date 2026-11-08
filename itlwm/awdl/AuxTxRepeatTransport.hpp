// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "AuxTxRepeat.hpp"
namespace ItlAwdl {
// Candidate drop-in ownership adapter. Not included by the installed driver.
// Policy and the actual descriptor transport share this one ownership object.
class AuxTxRepeatTransport : public AuxTxOwnership {
public:
    using Clock = uint64_t (*)();
    AuxTxRepeatTransport() : policy_(*this) {}
    bool armRepeat(uint32_t epoch, unsigned q, uint64_t now, uint64_t deadline,
                   unsigned budget, Clock clock) {
        if (repeat_ || attempted || pending || !clock || !policy_.arm(epoch,q,now,deadline,budget)) return false;
        repeat_ = true; clock_ = clock; deadline_ = deadline; return true;
    }
    bool repeatEnabled() const { return repeat_; }
    bool prepareRepeat(uint32_t epoch, uint32_t window, unsigned q, unsigned slot, bool idle) {
        if (!repeat_ || !policy_.begin(epoch,clock_(),window,q,slot,idle)) return false;
        prepared_ = true; queue_ = q; slot_ = slot; return true;
    }
    bool attempt() {
        if (!repeat_) return AuxTxOwnership::attempt();
        if (!prepared_ || !policy_.reserved()) return false;
        prepared_ = false; return true; // policy.begin already marked this attempt
    }
    bool submit(uint32_t epoch, unsigned q, unsigned slot, uint64_t deadline) {
        if (!repeat_) return AuxTxOwnership::submit(epoch,q,slot,deadline);
        if (prepared_ || q != queue_ || slot != slot_) return false;
        return policy_.submit(epoch,clock_(),deadline < deadline_ ? deadline : deadline_);
    }
    void skipped() {
        if (!repeat_) { AuxTxOwnership::skipped(); return; }
        policy_.skipped(); prepared_ = false;
    }
    bool complete(uint32_t epoch, unsigned q, unsigned slot) {
        return repeat_ ? policy_.complete(epoch,q,slot) : AuxTxOwnership::complete(epoch,q,slot);
    }
    bool requestFlush(uint32_t epoch, uint64_t deadline) {
        return repeat_ ? policy_.requestFlush(epoch,deadline) : AuxTxOwnership::requestFlush(epoch,deadline);
    }
    void quarantine() {
        if (repeat_) policy_.quarantine(); else AuxTxOwnership::quarantine();
    }
    void deviceQuiesced() {
        if (repeat_) policy_.deviceQuiesced(); else AuxTxOwnership::deviceQuiesced();
        prepared_ = false;
    }
    void pollRepeat(uint32_t epoch) {
        if (repeat_ && !policy_.poll(epoch,clock_())) stopPendingDeadline();
    }
    void cancelRepeat() { if (repeat_) { policy_.cancel(); stopPendingDeadline(); } }
    AuxTxRepeat::Stop repeatStop() const { return policy_.reason(); }
    unsigned repeatAttempts() const { return policy_.attempts(); }
private:
    void stopPendingDeadline() {
        // Let the real runtime request a flush even if ROC is still Running.
        // Never shorten an already outstanding cleanup wait or clear pending.
        if (pending && !flushSent) deadlineNs = 0;
    }
    AuxTxRepeat policy_;
    Clock clock_ = nullptr;
    uint64_t deadline_ = 0;
    unsigned queue_ = 0, slot_ = 0;
    bool repeat_ = false, prepared_ = false;
};
}
