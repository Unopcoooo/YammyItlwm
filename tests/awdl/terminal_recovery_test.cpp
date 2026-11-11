#include <errno.h>
#include "IbssDataTrial.hpp"
#include <assert.h>
#include <stdio.h>

struct State {
    enum class Phase { Idle, Associated, Failed };
    Phase phase=Phase::Associated;
    bool resetProven=true,ownsRadio=false,resetRequested=false,operation=false;
    struct { int active()const{return 0;} } ordinary;
    struct { bool idle()const{return true;} } commandAuthority;
    int primaryError=0,recoveryError=0;
    struct {
        bool commandInFlight=false,needsReset=false;
        struct { bool valid()const{return false;} } resources;
    } session;
};

struct Control {
    enum Phase { Idle, Failed };
    Phase phase=Failed;
    uint32_t reason=9,submitted=4096,completed=4096;
    void *port=nullptr;
    struct { bool pending=false;uint32_t submitted=4096,completed=4096; } schedule;
};

int main(){
    State state;Control control;
    assert(ItlIbss::terminalDataTrialReusable(state,control));
    for(unsigned fault=0;fault<14;++fault){
        auto s=state;auto c=control;
        if(fault==0)s.phase=State::Phase::Failed;
        if(fault==1)s.resetProven=false;
        if(fault==2)s.ownsRadio=true;
        if(fault==3)s.resetRequested=true;
        if(fault==4)s.operation=true;
        if(fault==5)s.primaryError=5;
        if(fault==6)s.recoveryError=5;
        if(fault==7)s.session.commandInFlight=true;
        if(fault==8)s.session.needsReset=true;
        if(fault==9)c.phase=Control::Idle;
        if(fault==10)c.reason=8;
        if(fault==11)c.port=&c;
        if(fault==12)c.schedule.pending=true;
        if(fault==13)c.completed=4095;
        assert(!ItlIbss::terminalDataTrialReusable(s,c));
    }
    puts("PASS terminal recovery requires retired radio, clean ordinary STA and fully drained control TX");
}
