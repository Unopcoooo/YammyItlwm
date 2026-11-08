// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
namespace ItlP2p {
// Host-only command planning. No firmware I/O or permission to run a trial.
constexpr uint32_t Invalid = 0xffffffffu;
struct BindingSnapshot {
    bool known = false, present = false;
    uint32_t generation = 0, revision = 0;
    uint32_t binding = Invalid, phy = Invalid, lmac = 0;
    uint32_t macs[3] = {Invalid, Invalid, Invalid};
};
enum class Refusal { None, Unknown, Epoch, Context, CorruptMembers, AlreadyMember, MissingMember, Full };
struct BindingEdit {
    BindingSnapshot basis;
    Refusal refusal = Refusal::Unknown;
    uint32_t generation = 0, revision = 0, action = 0;
    uint32_t binding = Invalid, phy = Invalid, lmac = 0;
    uint32_t macs[3] = {Invalid, Invalid, Invalid};
    bool ready() const { return refusal == Refusal::None; }
};
inline bool context(uint32_t word, unsigned limit) {
    return !(word & 0xffff0000u) && (word & 0xffu) < limit;
}
// Membership is authoritative only after native command completion. A pending
// or ambiguous command must make the caller's snapshot unknown until resolved
// or reset. queued==0, a global BINDING_ACTIVE flag and phy.ref are insufficient.
inline BindingEdit planBindingEdit(const BindingSnapshot &s, uint32_t generation,
                                   uint32_t mac, bool adding) {
    BindingEdit out;
    if (!s.known) return out;
    if (!generation || s.generation != generation) { out.refusal=Refusal::Epoch; return out; }
    if (!context(s.binding,3) || !context(s.phy,3) || !context(mac,4) || s.lmac>1) {
        out.refusal=Refusal::Context; return out;
    }
    unsigned count=0; int match=-1;
    for(unsigned i=0;i<3;++i) {
        if(s.macs[i]==Invalid) continue;
        if(!s.present || !context(s.macs[i],4)) {out.refusal=Refusal::CorruptMembers;return out;}
        for(unsigned j=0;j<i;++j)
            if(s.macs[j]!=Invalid && (s.macs[j]&0xffu)==(s.macs[i]&0xffu)) {
                out.refusal=Refusal::CorruptMembers;return out;
            }
        if((s.macs[i]&0xffu)==(mac&0xffu)) {
            if(s.macs[i]!=mac) {out.refusal=Refusal::Context;return out;} // stale color
            match=int(i);
        }
        ++count;
    }
    if(s.present && !count) {out.refusal=Refusal::CorruptMembers;return out;}
    if(adding && match>=0) {out.refusal=Refusal::AlreadyMember;return out;}
    if(!adding && match<0) {out.refusal=Refusal::MissingMember;return out;}
    if(adding && count==3) {out.refusal=Refusal::Full;return out;}
    out.basis=s;out.generation=generation;out.revision=s.revision;
    out.binding=s.binding;out.phy=s.phy;out.lmac=s.lmac;
    unsigned at=0;
    for(unsigned i=0;i<3;++i) if(s.macs[i]!=Invalid && int(i)!=match) out.macs[at++]=s.macs[i];
    if(adding) out.macs[at++]=mac;
    out.action=adding ? (s.present?2:1) : (at?2:3);
    out.refusal=Refusal::None;return out;
}
// Recheck under the actual driver's command gate before submission. This does
// not hold that gate across native synchronous waits; integration must track
// pending reservations separately, invalidate on reset and check again on ACK.
inline bool matchesSnapshot(const BindingEdit &e,const BindingSnapshot &s) {
    if (!(e.ready() && s.known && e.generation==s.generation && e.revision==s.revision &&
        e.binding==s.binding && e.phy==s.phy && e.lmac==s.lmac && e.basis.present==s.present)) return false;
    for(unsigned i=0;i<3;++i) if(e.basis.macs[i]!=s.macs[i]) return false;
    return true;
}
}
