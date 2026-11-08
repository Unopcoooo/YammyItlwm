// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "IbssWait.hpp"
int ItlIwm::tsleep_nsec(void *event,int priority,const char *message,int timeoutNs) {
    if (!awdlIbssOneShot.armed || !getMainWorkLoop()->inGate() || !awdlIbssExclusive.operation)
        return ItlHalService::tsleep_nsec(event,priority,message,timeoutNs);
    unsigned kind=event==&com.sc_fw && strcmp(message,"iwmfw")==0 ? 1 :
        event==&com.sc_uc ? 2 : event==&com.sc_init_complete ? 3 :
        event==&com.sc_ic.ic_state ? 4 : 0;
    if (!kind || timeoutNs<=0 || getMainWorkLoop()->onThread()) {
        awdlIbssExperimentEvent(10,0x20000|kind,ENOTSUP,0,0);return ENOTSUP;
    }
    struct Transport {
        ItlIwm *hal;void *event;
        bool allowed()const {return hal->getMainWorkLoop()->inGate() && !hal->getMainWorkLoop()->onThread();}
        bool current(uint32_t gen)const{return !hal->dmaDetaching && uint32_t(hal->com.sc_generation)==gen &&
            (hal->com.sc_ic.ic_if.if_flags&IFF_UP) && !(hal->com.sc_flags&IWM_FLAG_RFKILL);}
        bool expired(uint64_t deadline)const{uint64_t now;clock_get_uptime(&now);return now>=deadline;}
        int sleep(uint64_t deadline){
            int result=hal->getMainCommandGate()->commandSleep(event,deadline,THREAD_UNINT);
            return result==THREAD_AWAKENED?0:result==THREAD_TIMED_OUT?EAGAIN:EINTR;
        }
    } transport{this,event};
    auto ready=[&]{return kind==1 ? com.sc_fw_chunk_done!=0 : kind==2 ? com.sc_uc.uc_intr!=0 :
        kind==3 ? (com.sc_init_complete&(IWM_INIT_COMPLETE|IWM_CALIB_COMPLETE))==(IWM_INIT_COMPLETE|IWM_CALIB_COMPLETE) :
        com.sc_ic.ic_state==IEEE80211_S_SCAN;};
    uint64_t deadline;clock_interval_to_deadline(uint32_t(timeoutNs),kNanosecondScale,&deadline);
    awdlIbssExperimentEvent(9,0x20000|kind,0,unsigned(com.sc_generation),unsigned(timeoutNs));
    int error=ItlIbss::waitFor(transport,ready,uint32_t(com.sc_generation),deadline);
    awdlIbssExperimentEvent(10,0x20000|kind,error,unsigned(com.sc_generation),0);return error;
}
void ItlIwm::wakeupOn(void *event) {
    ItlHalService::wakeupOn(event); // Existing off-gate STA waiters remain unchanged.
    if(awdlIbssOneShot.armed && getMainWorkLoop()->inGate() &&
        (event==&com.sc_fw || event==&com.sc_uc || event==&com.sc_init_complete || event==&com.sc_ic.ic_state))
        getMainCommandGate()->commandWakeup(event,false);
}
