// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "NativeReceivePolicy.hpp"
#include "IbssRecoveryTiming.hpp"
#include "IbssIwmCommands.hpp"
#include "IbssCommandAuthority.hpp"
namespace ItlIbss {
enum class ExclusivePhase : uint8_t {
    Idle, Quiescing, FirmwareReady, Restoring, AwaitAssociation, Associated,
    Cancelled, Failed
};
class AtomicFlag {
    int32_t value = 0;
public:
    operator bool() const { return __atomic_load_n(&value,__ATOMIC_SEQ_CST) != 0; }
    AtomicFlag &operator=(bool v) { __atomic_store_n(&value,v ? 1 : 0,__ATOMIC_SEQ_CST); return *this; }
};
class OrdinaryOperations {
    int32_t count = 0;
public:
    void enter() { __atomic_add_fetch(&count,1,__ATOMIC_SEQ_CST); }
    void leave() { __atomic_sub_fetch(&count,1,__ATOMIC_SEQ_CST); }
    int32_t active() const { return __atomic_load_n(&count,__ATOMIC_SEQ_CST); }
};
struct ExclusiveState {
    ExclusivePhase phase = ExclusivePhase::Idle;
    AtomicFlag ownsRadio, resetRequested;
    OrdinaryOperations ordinary;
    CommandAuthority commandAuthority;
    bool operation = false, resetProven = false, nativeSearchRecovery = false;
    uint32_t generation = 0;
    uint64_t deadline = 0, recoveryDeadline = 0;
    int lastError = 0, primaryError = 0, recoveryError = 0;
    IwmSession session;
};
// IRQ faults quarantine new exclusive commands independently of diagnostics.
inline bool requestExclusiveReset(ExclusiveState &s) {
    if(!s.ownsRadio)return false;
    s.resetRequested=true;return true;
}
// Acquire/scan/state/TX entry points retain this permit over synchronous sleeps.
// Exclusive begin stores ownsRadio then rechecks active(), closing both sides
// of the race with a permit which was entering concurrently.
class OrdinaryPermit {
    ExclusiveState &s; bool held = false;
public:
    explicit OrdinaryPermit(ExclusiveState &state) : s(state) {
        s.ordinary.enter();
        if (!s.ownsRadio) held = true; else s.ordinary.leave();
    }
    ~OrdinaryPermit() { if (held) s.ordinary.leave(); }
    explicit operator bool() const { return held; }
    OrdinaryPermit(const OrdinaryPermit &) = delete;
    OrdinaryPermit &operator=(const OrdinaryPermit &) = delete;
};
class FirmwareCommandPermit {
    ExclusiveState &s; bool ordinary = false, allowed = false;
public:
    FirmwareCommandPermit(ExclusiveState &state, uintptr_t thread) : s(state) {
        if (s.ownsRadio) { allowed = !s.resetRequested && s.commandAuthority.allows(thread); return; }
        s.ordinary.enter();
        if (!s.ownsRadio) { ordinary = allowed = true; }
        else s.ordinary.leave();
    }
    ~FirmwareCommandPermit() { if (ordinary) s.ordinary.leave(); }
    explicit operator bool() const { return allowed; }
    FirmwareCommandPermit(const FirmwareCommandPermit &) = delete;
    FirmwareCommandPermit &operator=(const FirmwareCommandPermit &) = delete;
};
// Transport methods are shared by host fault-injection tests and the real IWM
// adapter. FirmwareReady is only an exclusive bootstrap, NOT active IBSS.
template<class T> class Exclusive {
    T &t; ExclusiveState &s;
    struct Operation {
        bool &flag;
        explicit Operation(bool &f) : flag(f) { flag = true; }
        ~Operation() { flag = false; }
    };
    int fail(int e, bool retainOwner) {
        s.lastError = e; if(!s.primaryError)s.primaryError=e; s.phase = ExclusivePhase::Failed;
        s.ownsRadio = retainOwner; return e;
    }
    int stopped() {
        uint32_t previous = t.generation();
        s.generation = previous; // Preserve proof epoch even if this stop fails.
        t.stop();
        s.resetProven = t.stoppedProof(previous);
        if (!s.resetProven) return fail(EIO,true);
        s.generation = t.generation();
        s.session = IwmSession(); // Only after DMA/reset proof, never on timeout.
        return 0;
    }
    int restore(uint64_t now) {
        s.phase = ExclusivePhase::Restoring;
        int e = stopped(); if (e) {s.recoveryError=e;return e;}
        if (!t.administrativelyUp()) {
            s.ownsRadio = false; s.phase = ExclusivePhase::Cancelled; return ECANCELED;
        }
        // Release the radio to the NORMAL state machine before its init/scan.
        // phase+operation still prohibit another exclusive begin/reentrant end.
        s.ownsRadio = false;
        e = t.startSta();
        if (!t.administrativelyUp()) {
            s.phase = ExclusivePhase::Cancelled; return ECANCELED;
        }
        if (e) {s.recoveryError=e;return fail(e,false);}
        s.generation = t.generation();
        const uint64_t allowance = s.nativeSearchRecovery ? NativeSearchJoinNs : OrdinaryJoinNs;
        if (now > UINT64_MAX - allowance) return fail(EOVERFLOW,false);
        s.recoveryDeadline = now + allowance;
        s.phase = ExclusivePhase::AwaitAssociation;
        return 0;
    }
public:
    Exclusive(T &transport, ExclusiveState &state) : t(transport), s(state) {}
    int begin(uint64_t now, uint64_t duration, bool nativeSearch = false) {
        if (s.phase != ExclusivePhase::Idle || s.operation) return EBUSY;
        if (!duration || duration > (nativeSearch?ItlAwdl::kNativeReceiveValidationNs:30000000000ull) || now > UINT64_MAX - duration || now > UINT64_MAX - 60000000000ull)
            return EINVAL;
        if (!t.canBegin() || !t.administrativelyUp()) return EBUSY;
        Operation operation(s.operation);
        s.ownsRadio = true;
        if (s.ordinary.active()) { s.ownsRadio = false; return EBUSY; }
        s.nativeSearchRecovery = nativeSearch;
        s.phase = ExclusivePhase::Quiescing;
        s.deadline = now + duration;
        int e = stopped(); if (e) return e;
        if (!t.administrativelyUp()) {
            s.ownsRadio = false; s.phase = ExclusivePhase::Cancelled; return ECANCELED;
        }
        s.resetProven = false; // Bootstrap can start DMA again.
        e = t.startExclusive();
        if (t.generation() != s.generation) {
            if (!t.stoppedProof(s.generation)) return fail(EIO,true);
            s.resetProven = true; s.session = IwmSession(); s.ownsRadio = false;
            s.phase = ExclusivePhase::Cancelled; return ECANCELED;
        }
        if (e || !t.exclusiveFirmwareProof()) {
            int cause = e ? e : EIO;
            s.lastError = s.primaryError = cause;
            // One bounded reset+ordinary recovery attempt, no automatic loop.
            int recovery = restore(t.now());
            return recovery ? recovery : cause;
        }
        if (!t.administrativelyUp()) return restore(t.now());
        if (!t.reserveFresh(s.session)) {
            s.lastError = s.primaryError = EBUSY;
            int recovery = restore(t.now()); return recovery ? recovery : EBUSY;
        }
        s.phase = ExclusivePhase::FirmwareReady;
        if (t.now() >= s.deadline) return restore(t.now());
        return 0;
    }
    int end(uint64_t now) {
        if (s.operation || s.phase != ExclusivePhase::FirmwareReady) return EBUSY;
        Operation operation(s.operation);
        // A different generation is not ours to modify. Release only if an
        // external stop has independently established the same DMA proof.
        if (t.generation() != s.generation) {
            if (!t.stoppedProof(s.generation)) return fail(EIO,true);
            s.resetProven = true; s.session = IwmSession(); s.ownsRadio = false;
            s.phase = ExclusivePhase::Cancelled; return ECANCELED;
        }
        return restore(now);
    }
    int poll(uint64_t now) {
        if (s.operation) return EBUSY;
        if (s.phase == ExclusivePhase::FirmwareReady &&
            (now >= s.deadline || !t.administrativelyUp() ||
             t.generation() != s.generation || s.session.needsReset || s.resetRequested)) return end(now);
        if (s.phase == ExclusivePhase::Failed && s.ownsRadio && t.stoppedProof(s.generation)) {
            s.resetProven = true; s.session = IwmSession(); s.ownsRadio = false;
            s.phase = ExclusivePhase::Cancelled; return ECANCELED;
        }
        if (s.phase != ExclusivePhase::AwaitAssociation) return 0;
        if(s.ownsRadio || !s.resetProven)return fail(EIO,bool(s.ownsRadio));
        if (!t.administrativelyUp()) {
            s.phase = ExclusivePhase::Cancelled; return ECANCELED;
        }
        if (now >= s.recoveryDeadline) return fail(ETIMEDOUT,false);
        // Original exclusive DMA is gone. Observe the normal stack's current
        // authorized link without modifying its generation or extending time.
        if (t.generation() && t.associatedAndAuthorized()) { s.phase = ExclusivePhase::Associated; return 0; }
        return EAGAIN;
    }
};
} // namespace ItlIbss
