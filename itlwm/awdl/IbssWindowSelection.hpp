// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "IbssExperiment.hpp"
namespace ItlIbss {
// Four finite numeric rows per actual arm; no additional scheduling or I/O.
inline ExperimentRows::Row selectionRow(const WindowPlan &p,uint64_t host,unsigned part) {
    using H=ExperimentRows;H::Row r;const auto us=host/1000;
    r.v[H::Kind]=part==3?30:(part?29:28);r.v[H::Code]=part==3?0:(part?part-1:0);
    r.v[H::HostUsLow]=uint32_t(us);r.v[H::HostUsHigh]=uint32_t(us>>32);
    r.v[H::Generation]=p.generation;r.v[H::MacContext]=p.peer;r.v[H::Length]=p.epoch;
    r.v[H::Phase]=p.open;r.v[H::Gp2]=p.observedGp2;
    if(!part) {
        r.v[H::Flags]=p.sequenceEncoding|(p.sequenceDuplicates<<8);
        r.v[H::Status]=p.sequenceCount|(p.sequenceSteps<<16);r.v[H::Valid]=p.sequenceFill;
        r.v[H::FrameControl]=p.boundarySequence;r.v[H::Channel]=6;
        r.v[H::Detail0]=p.awPeriod|(p.presence<<16);r.v[H::Detail1]=p.selectedSlot|(p.candidateOffset<<16);
        r.v[H::Result]=p.downCounter;
    } else if(part==3) {
        r.v[H::Detail0]=p.rawAwSequence;r.v[H::Detail1]=p.txDelayUs;
        r.v[H::Valid]=1; // Complete raw boundary arithmetic input, version1.
        r.v[H::Status]=p.version; // v1 original8TU; v2 prospective guarded EAW interior.
    } else if(part<=2) {
        const unsigned at=(part-1)*8;const unsigned fields[]={H::Detail0,H::Detail1,H::Status,H::Valid};
        for(unsigned i=0;i<4;++i)r.v[fields[i]]=uint32_t(p.sequenceEntries[at+i*2])|(uint32_t(p.sequenceEntries[at+i*2+1])<<16);
    }
    return r;
}
}
