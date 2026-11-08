// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "BindingPlan.hpp"
namespace ItlP2p {
// Fixed storage, main-gate caller only. This is an observation ledger, NOT an
// allocator or a replacement for ordinary/exclusive command authorization.
// Native command-slot completion and explicit FW status remain distinct.
enum class ResourceKind : uint8_t { Phy, Mac, Binding };
enum class Presence : uint8_t { Unknown, Absent, Present };
enum class Completion : uint8_t { NotSubmitted, Ambiguous, NativeCompleted, StatusAccepted };
struct ResourceCommand {
    ResourceKind kind=ResourceKind::Phy;
    uint32_t context=Invalid, action=0;
    uint32_t channel=0, role=0, tsf=0, phy=Invalid, lmac=0;
    uint32_t macs[3]={Invalid,Invalid,Invalid};
};
struct ResourceFact {
    ResourceCommand command;
    Presence presence=Presence::Unknown;
    Completion proof=Completion::Ambiguous;
    uint64_t revision=0, pending=0;
    bool quarantined=false;
};
struct ResourceReceipt {
    ResourceCommand command;
    ResourceFact before;
    uint32_t generation=0;
    uint64_t epoch=0, ticket=0;
    bool valid()const {return ticket!=0;}
};
class ResourceLedger {
    ResourceFact phys[3],macs[4],bindings[3];
    uint32_t generation=0;
    uint64_t epoch=0,serial=0;
    bool exhausted=false;
    static unsigned limit(ResourceKind k) {
        return k==ResourceKind::Phy?3:k==ResourceKind::Mac?4:k==ResourceKind::Binding?3:0;
    }
    ResourceFact *slot(ResourceKind k,unsigned id) {
        if(id>=limit(k))return nullptr;
        return k==ResourceKind::Phy?&phys[id]:k==ResourceKind::Mac?&macs[id]:&bindings[id];
    }
    static bool payloadValid(const ResourceCommand &c) {
        if(!context(c.context,limit(c.kind)) || c.action<1 || c.action>3)return false;
        if(c.kind!=ResourceKind::Binding)return true;
        if(!context(c.phy,3) || c.lmac>1)return false;
        unsigned count=0;
        for(unsigned i=0;i<3;++i) {
            if(c.macs[i]==Invalid)continue;
            if(!context(c.macs[i],4))return false;
            for(unsigned j=0;j<i;++j)
                if(c.macs[j]!=Invalid && (c.macs[j]&255)==(c.macs[i]&255))return false;
            ++count;
        }
        return c.action==3 || count!=0;
    }
    void unknown(ResourceFact &s,bool quarantine) {
        s.presence=Presence::Unknown;s.proof=Completion::Ambiguous;s.pending=0;
        s.quarantined=quarantine;
        if(s.revision==UINT64_MAX){exhausted=true;return;}
        ++s.revision;
    }
public:
    // A driver generation change is NOT proof that all resources are absent.
    // Even a known fresh FW begins with Unknown until its commands are seen.
    // Call before the first command of each generation and on reset/fault/DMA
    // teardown. Same generation invalidation still revokes every old receipt.
    void invalidate(uint32_t g) {
        generation=g;
        if(epoch==UINT64_MAX){exhausted=true;return;}
        ++epoch;
        for(auto &s:phys)s=ResourceFact();
        for(auto &s:macs)s=ResourceFact();
        for(auto &s:bindings)s=ResourceFact();
    }
    const ResourceFact *get(ResourceKind k,unsigned id)const {
        return const_cast<ResourceLedger*>(this)->slot(k,id);
    }
    // Observe an actual imminent native command, not a native helper's early
    // return0. An overlapping command poisons this resource until invalidation;
    // ACK order alone cannot reconstruct side effects of two conflicting waits.
    ResourceReceipt begin(const ResourceCommand &c,uint32_t g) {
        ResourceReceipt r;
        if(exhausted || !g || g!=generation || !context(c.context,limit(c.kind)))return r;
        auto &s=*slot(c.kind,c.context&255);
        if(!payloadValid(c)){unknown(s,true);return r;}
        if(s.quarantined)return r;
        if(s.pending){unknown(s,true);return r;}
        if(serial==UINT64_MAX || s.revision==UINT64_MAX){exhausted=true;unknown(s,true);return r;}
        r.command=c;r.before=s;r.generation=g;r.epoch=epoch;r.ticket=++serial;
        s.pending=r.ticket;s.presence=Presence::Unknown;s.proof=Completion::Ambiguous;++s.revision;
        return r;
    }
    // Return false for stale/duplicate/overlapped completions. No automatic
    // resource release on timeout, async queueing, error or changed epoch.
    bool finish(const ResourceReceipt &r,uint32_t g,Completion result) {
        if(exhausted || !r.valid() || g!=generation || g!=r.generation || epoch!=r.epoch)return false;
        auto target=slot(r.command.kind,r.command.context&255);
        if(!target || target->quarantined || target->pending!=r.ticket)return false;
        auto &s=*target;
        if(s.revision==UINT64_MAX){exhausted=true;unknown(s,true);return false;}
        const auto revision=s.revision+1;
        if(result==Completion::NotSubmitted) {s=r.before;s.revision=revision;return true;}
        if(result==Completion::Ambiguous) {unknown(s,true);return true;}
        if(result!=Completion::NativeCompleted && result!=Completion::StatusAccepted) {
            unknown(s,true);return false;
        }
        s.command=r.command;s.pending=0;s.revision=revision;s.proof=result;
        if(r.command.action==3)s.presence=Presence::Absent;
        else if(r.command.action==1 || (r.before.presence==Presence::Present &&
                r.before.command.context==r.command.context))s.presence=Presence::Present;
        else {unknown(s,true);return true;} // MODIFY cannot establish prior existence
        return true;
    }
    BindingSnapshot binding(unsigned id)const {
        BindingSnapshot b;auto s=get(ResourceKind::Binding,id);
        if(!s || exhausted || s->pending || s->quarantined || !generation ||
           s->presence==Presence::Unknown || s->revision>UINT32_MAX ||
           s->proof!=Completion::StatusAccepted)return b;
        // This is a diagnostic candidate for planBindingEdit. Dependencies,
        // resource reservations and complete source coverage are still required.
        b.known=true;b.present=s->presence==Presence::Present;b.generation=generation;
        b.revision=uint32_t(s->revision);b.binding=s->command.context;
        b.phy=s->command.phy;b.lmac=s->command.lmac;
        if(b.present)for(unsigned i=0;i<3;++i)b.macs[i]=s->command.macs[i];
        return b;
    }
};
}
