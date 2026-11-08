// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>

namespace ItlIbss {
// Policy only: these actions have NO device implementation in this candidate.
// Station actions include their reserved DQA queue resources in a future adapter.
enum class Action : uint8_t {
    None, QuiesceSta, ConfigurePhy, UploadBeacon, AddMac, AddBinding,
    AddMcast, AddBcast, UpdatePower, GrantQuota,
    RevokeQuota, RemoveBcast, RemoveMcast, RemoveBinding, RestorePower,
    RemoveMac, RestorePhy, RestoreSta, AddListenerMac, AddSniffer, ListenerPower
};
enum class Phase : uint8_t { Idle, Starting, Active, Stopping, Done, NeedsReset };
// Rejected means the adapter PROVED no side effects for this action. A transport
// error/partial queue allocation is Unknown unless rollback was confirmed.
enum class Reply : uint8_t { Applied, Rejected, Unknown };
struct Token { uint32_t generation, serial; Action action; };
struct Reservation {
    uint8_t mac, tsf, phy, mcastSta, bcastSta, mcastQueue, bcastQueue;
    // Must be attested by a future allocator after checking ALL live resources.
    bool exclusivelyReserved;
    bool valid() const {
        return exclusivelyReserved && mac < 4 && tsf < 4 && phy < 3 &&
            mcastSta > 2 && mcastSta < 16 && bcastSta > 2 && bcastSta < 16 &&
            mcastSta != bcastSta && mcastQueue == 3 && bcastQueue == 9;
    }
};
class Lifecycle {
    enum : uint16_t { Sta = 1, Phy = 2, Beacon = 4, Mac = 8, Binding = 16,
        Mcast = 32, Bcast = 64, Power = 128, Quota = 256 };
    uint16_t owned_ = 0;
    uint32_t generation_ = 0, serial_ = 0;
    uint64_t deadline_ = 0;
    unsigned step_ = 0;
    bool pending_ = false, stop_ = false;
    Token inFlight_ = {};
    Phase phase_ = Phase::Idle;
    Action cleanup() const {
        if (owned_ & Quota) return Action::RevokeQuota;
        // STA_TYPE firmware: remove bcast BEFORE mcast, unlike reverse add.
        if (owned_ & Bcast) return Action::RemoveBcast;
        if (owned_ & Mcast) return Action::RemoveMcast;
        if (owned_ & Binding) return Action::RemoveBinding;
        if (owned_ & Power) return Action::RestorePower;
        if (owned_ & Mac) return Action::RemoveMac;
        if (owned_ & Beacon) return Action::None; // No proven standalone delete.
        if (owned_ & Phy) return Action::RestorePhy;
        if (owned_ & Sta) return Action::RestoreSta;
        return Action::None;
    }
public:
    Phase phase() const { return phase_; }
    uint16_t retainedResources() const { return owned_; }
    bool pending() const { return pending_; }
    bool begin(uint32_t generation, uint64_t now, uint64_t duration,
               const Reservation &r, bool staType, bool beaconV9, bool dqa) {
        if (phase_ != Phase::Idle || !generation || !r.valid() || !staType ||
            !beaconV9 || !dqa || !duration || duration > 30000000000ull ||
            now > UINT64_MAX - duration) return false;
        generation_ = generation; deadline_ = now + duration;
        phase_ = Phase::Starting;
        return true;
    }
    void stop() { stop_ = true; }
    Action next(uint64_t now) {
        if (phase_ == Phase::Idle || phase_ == Phase::Done || phase_ == Phase::NeedsReset)
            return Action::None;
        if (pending_) {
            if (now >= deadline_) phase_ = Phase::NeedsReset;
            return Action::None; // Never free unknown in-flight ownership.
        }
        if (stop_ || now >= deadline_) phase_ = Phase::Stopping;
        Action a = Action::None;
        if (phase_ == Phase::Starting) {
            const Action order[] = {Action::QuiesceSta, Action::ConfigurePhy,
                Action::UploadBeacon, Action::AddMac, Action::AddBinding,
                Action::AddMcast, Action::AddBcast, Action::UpdatePower, Action::GrantQuota};
            a = order[step_];
        } else if (phase_ == Phase::Stopping) {
            a = cleanup();
            if (a == Action::None) phase_ = owned_ ? Phase::NeedsReset : Phase::Done;
        }
        return a;
    }
    bool issue(Action a, uint64_t now, Token &token) {
        if (a == Action::None || pending_ || next(now) != a || serial_ == UINT32_MAX)
            return false;
        token = {generation_, ++serial_, a}; inFlight_ = token; pending_ = true;
        return true;
    }
    bool complete(const Token &t, Reply result) {
        if (!pending_ || t.generation != generation_ || t.serial != inFlight_.serial ||
            t.action != inFlight_.action || phase_ == Phase::NeedsReset) return false;
        pending_ = false;
        if (result == Reply::Unknown) { phase_ = Phase::NeedsReset; return true; }
        if (result == Reply::Rejected) {
            phase_ = phase_ == Phase::Stopping ? Phase::NeedsReset : Phase::Stopping;
            return true;
        }
        switch (t.action) {
        case Action::QuiesceSta: owned_ |= Sta; break;
        case Action::ConfigurePhy: owned_ |= Phy; break;
        case Action::UploadBeacon: owned_ |= Beacon; break;
        case Action::AddMac: owned_ |= Mac; break;
        case Action::AddBinding: owned_ |= Binding; break;
        case Action::AddMcast: owned_ |= Mcast; break;
        case Action::AddBcast: owned_ |= Bcast; break;
        case Action::UpdatePower: owned_ |= Power; break;
        case Action::GrantQuota: owned_ |= Quota; break;
        case Action::RevokeQuota: owned_ &= ~Quota; break;
        case Action::RemoveBcast: owned_ &= ~Bcast; break;
        case Action::RemoveMcast: owned_ &= ~Mcast; break;
        case Action::RemoveBinding: owned_ &= ~Binding; break;
        case Action::RestorePower: owned_ &= ~Power; break;
        case Action::RemoveMac: owned_ &= ~(Mac | Beacon); break;
        case Action::RestorePhy: owned_ &= ~Phy; break;
        case Action::RestoreSta: owned_ &= ~Sta; break;
        default: return false;
        }
        if (phase_ == Phase::Starting && ++step_ == 9) phase_ = Phase::Active;
        return true;
    }
    // Only a confirmed device reset/quiescence invalidates uncertain ownership.
    // Caller must restore STA separately. Never declare restoration from reset.
    bool deviceResetConfirmed(uint32_t oldGeneration, uint32_t newGeneration) {
        if (phase_ != Phase::NeedsReset || oldGeneration != generation_ ||
            !newGeneration || newGeneration == oldGeneration) return false;
        owned_ = 0; pending_ = false; generation_ = newGeneration;
        phase_ = Phase::Done;
        return true;
    }
};
} // namespace ItlIbss
