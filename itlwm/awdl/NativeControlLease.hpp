// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "NativeSearchOwner.hpp"

namespace ItlAwdl {
// Offline policy only. Adapter must retain the interface, serialize access and
// supply the real gated backend predicate; this object never establishes readiness.
struct NativeControlLease {
    enum class Phase { Down, Publishing, Up, Withdrawing, Failed };
    Phase phase=Phase::Down;
    uint64_t epoch=0,deadlineNs=0;
    uint32_t generation=0;
    bool upInFlight=false,downInFlight=false;

    bool eligible(const NativeSearchOwner &owner,bool backendEnabled,uint64_t now) const {
        return backendEnabled&&owner.matches(epoch)&&owner.generation==generation&&
            owner.phase==NativeSearchOwner::Phase::Searching&&!owner.stopRequested&&
            !owner.expired&&!owner.failure&&now>=owner.startNs&&now<deadlineNs&&
            owner.deadlineNs==deadlineNs;
    }
    bool begin(const NativeSearchOwner &owner,bool backendEnabled,uint64_t now) {
        if(phase!=Phase::Down||!owner.epoch||owner.epoch<=epoch||!owner.generation)return false;
        NativeControlLease next;next.epoch=owner.epoch;next.generation=owner.generation;
        next.deadlineNs=owner.deadlineNs;
        if(!next.eligible(owner,backendEnabled,now))return false;
        next.phase=Phase::Publishing;next.upInFlight=true;*this=next;return true;
    }
    bool cancel(uint64_t expectedEpoch) {
        if(!expectedEpoch||expectedEpoch!=epoch||phase==Phase::Down||phase==Phase::Failed)return false;
        phase=Phase::Withdrawing;return true;
    }
    bool observe(uint64_t expectedEpoch,const NativeSearchOwner &owner,bool backendEnabled,uint64_t now) {
        if(!expectedEpoch||expectedEpoch!=epoch)return false;
        if((phase==Phase::Publishing||phase==Phase::Up)&&!eligible(owner,backendEnabled,now))
            phase=Phase::Withdrawing;
        return true;
    }
    bool published(uint64_t expectedEpoch,bool accepted,const NativeSearchOwner &owner,
                   bool backendEnabled,uint64_t now) {
        if(!expectedEpoch||expectedEpoch!=epoch||!upInFlight)return false;
        upInFlight=false;
        // Even an error can follow partial native side effects. Always withdraw.
        if(phase==Phase::Publishing&&accepted&&eligible(owner,backendEnabled,now))phase=Phase::Up;
        else phase=Phase::Withdrawing;
        return true;
    }
    bool beginWithdraw(uint64_t expectedEpoch) {
        if(!expectedEpoch||expectedEpoch!=epoch||phase!=Phase::Withdrawing||upInFlight||downInFlight)return false;
        downInFlight=true;return true;
    }
    bool withdrawn(uint64_t expectedEpoch,bool confirmedDown) {
        if(!expectedEpoch||expectedEpoch!=epoch||!downInFlight)return false;
        downInFlight=false;phase=confirmedDown?Phase::Down:Phase::Failed;return true;
    }
    bool releasable()const{return phase==Phase::Down&&!upInFlight&&!downInFlight;}
};
}
