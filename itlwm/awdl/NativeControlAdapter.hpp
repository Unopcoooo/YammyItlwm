// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "NativeControlLease.hpp"
namespace ItlAwdl {
// Offline adapter. Ops must implement a serialized gate, a lifecycle pin that
// protects native teardown (retain alone is insufficient), and qualified samples.
// The dispatcher must outlive all calls, including quarantined ownership.
template<class Ops> class NativeControlAdapter {
public:
    using Owner=typename Ops::Owner;
    struct Sample { NativeSearchOwner search;bool qualified=false;uint64_t now=0; };
private:
    NativeControlLease lease;
    Owner *owner=nullptr;
    bool calling=false;
    NativeControlAdapter(const NativeControlAdapter&)=delete;
    NativeControlAdapter& operator=(const NativeControlAdapter&)=delete;
public:
    NativeControlAdapter()=default;
    // Gate held. pin() must also prove initial link DOWN and exclusive ownership.
    bool begin(Ops &ops,Owner *candidate) {
        if(!ops.inGate()||owner||calling||!candidate)return false;
        auto s=ops.sample(candidate);auto next=lease;
        if(!next.begin(s.search,s.qualified,s.now)||!ops.pin(candidate))return false;
        owner=candidate;lease=next;return true;
    }
    bool cancel(Ops &ops,uint64_t epoch) {
        return ops.inGate()&&owner&&lease.cancel(epoch);
    }
    bool quarantined(Ops &ops)const {
        return ops.inGate()&&owner&&lease.phase==NativeControlLease::Phase::Failed;
    }
    bool idle(Ops &ops)const {
        return ops.inGate()&&!owner&&!calling&&lease.releasable();
    }
    // Call only from a retained dispatcher outside the IWM/driver/PM gates.
    // Native calls may reenter cancel(). Never wait for this method while holding
    // a gate that those calls need. This does not drive or retire DMA resources.
    bool pump(Ops &ops) {
        if(ops.inGate())return false;
        Owner *target=nullptr;uint64_t epoch=0;bool up=false;
        ops.withGate([&]{
            if(!owner||calling)return;
            auto s=ops.sample(owner);
            lease.observe(lease.epoch,s.search,s.qualified,s.now);
            if(lease.phase==NativeControlLease::Phase::Publishing)up=true;
            else {
                // An offer cancelled before dispatch still needs conservative DOWN.
                if(lease.upInFlight)lease.published(lease.epoch,false,s.search,s.qualified,s.now);
                if(!lease.beginWithdraw(lease.epoch))return;
            }
            target=owner;epoch=lease.epoch;calling=true;
        });
        if(!target)return false;
        // Return value is a confirmed link result, NOT PM configure/Peer success.
        const bool confirmed=up?ops.publish(target):ops.withdraw(target);
        ops.withGate([&]{
            // begin() cannot replace a pinned owner while a native call is pending.
            calling=false;
            if(up){
                auto s=ops.sample(owner);
                lease.published(epoch,confirmed,s.search,s.qualified,s.now);
            }else {
                lease.withdrawn(epoch,confirmed);
                if(lease.releasable()){
                    auto retired=owner;owner=nullptr;ops.unpin(retired);
                }
                // Failed withdrawal keeps the pin and rejects reuse. Teardown
                // needs a separately proved handoff; no destructor auto-release.
            }
        });
        return true;
    }
};
}
