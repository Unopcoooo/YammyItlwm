// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "IbssRecoveryTiming.hpp"
#include <stdint.h>
namespace ItlIbss {
// Read-only observation of ordinary STA after exclusive ownership was released.
// This type has no transport, firmware command, session or generation authority.
enum class StaRecoveryPhase : uint32_t { Idle=0,Watching=1,StableAtDeadline=2,NotStable=3,Faulted=4,Incomplete=5 };
// Read-only diagnostics: at most16 changes, one overflow marker and one terminal
// sample. No authority to alter recovery, retry association or extend deadlines.
struct RecoveryEligibilityTrace {
    uint32_t generation=0,mask=0,states=0,ifFlags=0,driverFlags=0,changes=0;
    bool seen=false,overflow=false,terminal=false;
    unsigned claim(uint32_t gen,uint32_t bits,uint32_t net,uint32_t iff,uint32_t driver,bool final) {
        if(final){if(terminal)return 0;terminal=true;return 3;}
        if(terminal)return 0;
        if(seen && generation==gen && mask==bits && states==net && ifFlags==iff && driverFlags==driver)return 0;
        seen=true;generation=gen;mask=bits;states=net;ifFlags=iff;driverFlags=driver;
        if(changes<16){++changes;return 1;}
        if(!overflow){overflow=true;return 2;}
        return 0;
    }
};
// A fresh admission proof; never rewrites the completed recovery verdict.
struct LateRecoveryAdmission {
    uint64_t since=0,last=0;uint32_t generation=0;
    bool sample(uint64_t now,uint32_t gen,bool eligible){
        if(!eligible||!now||!gen){*this=LateRecoveryAdmission();return false;}
        if(!since||gen!=generation||now<last||now-last>2000000000ULL){since=now;generation=gen;}
        last=now;return now-since>=10000000000ULL;
    }
};
struct StaRecoveryObservation {
    RecoveryEligibilityTrace eligibilityTrace;
    StaRecoveryPhase phase=StaRecoveryPhase::Idle;
    uint32_t initialGeneration=0,generation=0,generationChanges=0,samples=0,gaps=0,stableMs=0;
    uint64_t start=0,deadline=0,last=0,stableSince=0;
    bool begin(uint64_t now,uint32_t gen,bool radioOwned,bool operation,bool nativeSearch=false) {
        const uint64_t duration=nativeSearch ? NativeSearchObservationNs : OrdinaryObservationNs;
        if(phase!=StaRecoveryPhase::Idle || !gen || radioOwned || operation || now>UINT64_MAX-duration)return false;
        start=last=now;deadline=now+duration;initialGeneration=generation=gen;
        phase=StaRecoveryPhase::Watching;return true;
    }
    bool watching()const{return phase==StaRecoveryPhase::Watching;}
    void sample(uint64_t now,uint32_t gen,bool eligible,bool radioOwned,bool operation,uint32_t faults,bool lost) {
        if(!watching())return;
        ++samples;
        if(now<last){phase=StaRecoveryPhase::Incomplete;return;}
        bool gap=now-last>2000000000ull;
        if(gap){++gaps;stableSince=0;}
        if(gen!=generation){generation=gen;++generationChanges;stableSince=0;}
        last=now;
        if(!gen || !eligible || radioOwned || operation)stableSince=0;
        else if(!stableSince)stableSince=now?now:1;
        const uint64_t observedThrough=now<deadline?now:deadline;
        stableMs=stableSince && observedThrough>=stableSince?uint32_t((observedThrough-stableSince)/1000000ull):0;
        // Keep the original 60-second observation for fast recovery. If the
        // normal STA joins late, allow the same uninterrupted 10-second tail.
        const bool ready=eligible && gen && !radioOwned && !operation && stableMs>=10000;
        if(now<deadline && (now-start<OrdinaryObservationNs || (!ready && !faults && !lost)))return;
        if(faults)phase=StaRecoveryPhase::Faulted;
        else if(lost)phase=StaRecoveryPhase::Incomplete;
        else phase=ready?
            StaRecoveryPhase::StableAtDeadline:StaRecoveryPhase::NotStable;
    }
};
} // namespace ItlIbss
