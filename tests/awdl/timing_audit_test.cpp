#include "../../itlwm/awdl/TimingAudit.hpp"
#include <cassert>
#include <cstdio>
using namespace ItlAwdl;
static ChannelSequence channels() {
    ChannelSequence c={};c.count=16;c.steps=4;c.fill=0xffff;c.encoding=3;
    for(auto &e:c.entries)e.channel=6;
    return c;
}
int main() {
    SyncParameters s={};s.awPeriod=16;s.presenceMode=4;s.nextAwSequence=100;s.txDownCounter=32;
    auto c=channels();TimingAudit a;
    a.observe(s,c,100000);assert(a.streak==1&&!a.compared&&a.reason==TimingReason::FirstObservation);
    s.txDownCounter=31;a.observe(s,c,101024);assert(a.streak==2&&a.consistent&&a.residualUs==0&&a.gapUs==1024&&a.reason==TimingReason::Consistent);
    s.txDownCounter=30;a.observe(s,c,102048);assert(a.streak==3&&a.consistent);
    PeerRocPlan p;uint32_t reason;
    assert(planImminentPeerRoc(a,s,c,102048,124000,p,reason));
    assert(p.channel==6&&p.boundaryGp2==132768&&p.waitUs==8768&&p.streak==3);
    assert(!planImminentPeerRoc(a,s,c,102048,102048,p,reason)&&reason==13);
    assert(!planImminentPeerRoc(a,s,c,102048,700000,p,reason)&&reason==11);
    c.entries[10].channel=153;
    assert(!planImminentPeerRoc(a,s,c,102048,124000,p,reason)&&reason==12);
    c=channels();a.observe(s,c,102048);assert(a.streak==3&&!a.consistent&&a.reason==TimingReason::DuplicateTimestamp); // duplicate adds nothing
    s.txDownCounter=29;a.observe(s,c,103072);assert(a.streak==4&&a.consistent);
    s.txDownCounter=1;a.observe(s,c,104096);assert(a.streak==1&&a.compared&&!a.consistent);
    // GP2 and 16-bit AW counter wrap; stable boundary/counter alignment.
    TimingAudit wrap;s.nextAwSequence=65532;s.txDownCounter=2;
    wrap.observe(s,c,0xfffff800u);s.txDownCounter=1;wrap.observe(s,c,0xfffffc00u);
    s.nextAwSequence=0;s.txDownCounter=64;wrap.observe(s,c,0);
    assert(wrap.streak==3&&wrap.consistent&&wrap.residualUs==0);
    // Changed channel schedule resets confidence, including same channel but auxiliary change.
    c.entries[0].auxiliary=1;s.txDownCounter=63;wrap.observe(s,c,1024);assert(wrap.streak==1&&wrap.reason==TimingReason::ScheduleChanged&&wrap.changedSlot==0);
    // Equal phase but a wrong AW counter must not pass the audit.
    TimingAudit bad;s.nextAwSequence=100;s.txDownCounter=32;bad.observe(s,channels(),100000);
    s.nextAwSequence=104;s.txDownCounter=31;bad.observe(s,channels(),101024);
    assert(bad.compared&&!bad.consistent&&bad.streak==1&&bad.reason==TimingReason::ResidualTooLarge);
    // Quantization can put the two next-boundary estimates on adjacent EAWs.
    TimingAudit edge;s.nextAwSequence=100;s.txDownCounter=1;edge.observe(s,channels(),100000);
    edge.observe(s,channels(),101025);assert(edge.consistent&&edge.residualUs==1025);
    // Long gap and malformed period do not authorize any plan.
    edge.observe(s,channels(),800000);assert(edge.streak==1&&!edge.compared&&edge.reason==TimingReason::ObservationStale);
    s.presenceMode=0;edge.observe(s,channels(),801000);assert(!edge.consistent&&edge.reason==TimingReason::PeriodChanged);
    auto r=measureRocAlignment(98000,110000,100000,65536);
    assert(r.before&&r.distanceUs==2000&&r.overlapUs==10000);
    r=measureRocAlignment(103000,115000,100000,65536);assert(!r.before&&r.distanceUs==3000&&r.overlapUs==12000);
    r=measureRocAlignment(70000,80000,100000,65536);assert(r.overlapUs==0);
    r=measureRocAlignment(0xfffffff0u,0x1000,0,65536);assert(r.before&&r.distanceUs==16&&r.overlapUs==4096);
    puts("timing audit/planner: consecutive phase/counter agreement, schedule change, stale/lead gates, GP2/AW wrap and overlap PASS");
}
