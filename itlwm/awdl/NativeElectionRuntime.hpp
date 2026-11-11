// SPDX-License-Identifier: GPL-2.0-or-later
// Native election state is accepted only for the local-master profile that the
// host scheduler actually executes. The native metric is then used in PSF/MIF.
#pragma once
#include "NativeElectionParams.hpp"

void ItlIwm::awdlNativeElectionPublish(){
    auto *root=getController();if(!root)return;
    root->setProperty("AWDLNativeElectionValid",awdlNativeElectionValid);
    root->setProperty("AWDLNativeElectionMaster",awdlNativeElectionMaster,6);
    root->setProperty("AWDLNativeElectionRootMetric",awdlNativeElectionRootMetric,32);
    root->setProperty("AWDLNativeElectionSelfMetric",awdlNativeElectionSelfMetric,32);
    root->setProperty("AWDLNativeElectionParams",awdlNativeElectionRaw,sizeof(awdlNativeElectionRaw));
    root->setProperty("AWDLNativeElectionMetricValid",awdlNativeElectionMetricValid);
    root->setProperty("AWDLNativeElectionMetricRequested",awdlNativeElectionMetricRequested,32);
    root->setProperty("AWDLNativeElectionMetricOverride",awdlNativeElectionMetricOverride,8);
}

IOReturn ItlIwm::awdlNativeElectionParams(const void *payload,size_t length,OSObject *owner){
    auto loop=getMainWorkLoop();auto gate=getMainCommandGate();
    if(!loop||!gate)return kIOReturnNotReady;
    ItlAwdlRocLifetime lifetime(this,gate,loop);
    if(!loop->inGate()){
        struct Request{ItlIwm *hal;const void *payload;size_t length;OSObject *owner;} request{
            this,payload,length,owner};
        return gate->runAction([](OSObject*,void *arg,void*,void*,void*)->IOReturn{
            auto r=static_cast<Request*>(arg);
            return r->hal->awdlNativeElectionParams(r->payload,r->length,r->owner);
        },&request);
    }
    ItlAwdl::NativeElectionParams params;
    if(!ItlAwdl::parseNativeElectionParams(payload,length,params))return kIOReturnBadArgument;
    if(!owner||dmaDetaching||!awdlNativeSearchEnabled()||!awdlNativeSearch.active()||
       awdlNativeSearch.phase!=ItlAwdl::NativeSearchOwner::Phase::Searching||
       !awdlNativeSyncRoleReady()||
       awdlNativeControlTx.epoch!=awdlNativeSearch.epoch||
       awdlNativeControlTx.generation!=uint32_t(com.sc_generation)||
       !ItlNativeControlRoleAllowed(getController(),owner,awdlNativeSearch.epoch,false))
        return kIOReturnNotReady;
    // The current host scheduler implements only the measured local-master,
    // equal-metric profile. Reject any other election tree instead of claiming
    // that an unimplemented remote-master schedule was applied.
    if(memcmp(params.master,awdlNativeSearch.local,6)||
       params.masterMetric!=params.selfMetric||
       (awdlNativeElectionMetricValid&&
        awdlNativeElectionMetricRequested!=params.selfMetric))
        return kIOReturnUnsupported;
    memcpy(awdlNativeElectionRaw,payload,length);
    memcpy(awdlNativeElectionMaster,params.master,6);
    awdlNativeElectionRootMetric=params.masterMetric;
    awdlNativeElectionSelfMetric=params.selfMetric;
    awdlNativeElectionValid=true;
    awdlNativeElectionPublish();
    return kIOReturnSuccess;
}

IOReturn ItlIwm::awdlNativeElectionMetric(const void *payload,size_t length,OSObject *owner){
    auto loop=getMainWorkLoop();auto gate=getMainCommandGate();
    if(!loop||!gate)return kIOReturnNotReady;
    ItlAwdlRocLifetime lifetime(this,gate,loop);
    if(!loop->inGate()){
        struct Request{ItlIwm *hal;const void *payload;size_t length;OSObject *owner;} request{
            this,payload,length,owner};
        return gate->runAction([](OSObject*,void *arg,void*,void*,void*)->IOReturn{
            auto r=static_cast<Request*>(arg);
            return r->hal->awdlNativeElectionMetric(r->payload,r->length,r->owner);
        },&request);
    }
    ItlAwdl::NativeElectionMetric metric;
    if(!ItlAwdl::parseNativeElectionMetric(payload,length,metric))
        return kIOReturnBadArgument;
    if(!owner||dmaDetaching||!awdlNativeSearch.active()||
       !awdlNativeSearch.matches(awdlNativeSearch.epoch)||
       ItlAwdl::gDataPort.owner!=owner||ItlAwdl::gDataPort.releasePending)
        return kIOReturnNotReady;
    awdlNativeElectionMetricRequested=metric.metric;
    awdlNativeElectionMetricOverride=metric.overrideMetric;
    awdlNativeElectionMetricValid=true;
    // Once a local-master election is active, its two equal metrics follow the
    // native metric update and are consumed by the next real PSF/MIF frame.
    if(awdlNativeElectionValid){
        awdlNativeElectionRootMetric=metric.metric;
        awdlNativeElectionSelfMetric=metric.metric;
    }
    awdlNativeElectionPublish();
    return kIOReturnSuccess;
}

IOReturn ItlIwm::awdlNativeElectionMetricRead(uint32_t &metric,uint8_t &overrideMetric){
    auto loop=getMainWorkLoop();auto gate=getMainCommandGate();
    if(!loop||!gate)return kIOReturnNotReady;
    ItlAwdlRocLifetime lifetime(this,gate,loop);
    if(!loop->inGate()){
        struct Request{ItlIwm *hal;uint32_t *metric;uint8_t *overrideMetric;} request{
            this,&metric,&overrideMetric};
        return gate->runAction([](OSObject*,void *arg,void*,void*,void*)->IOReturn{
            auto r=static_cast<Request*>(arg);
            return r->hal->awdlNativeElectionMetricRead(*r->metric,*r->overrideMetric);
        },&request);
    }
    if(!awdlNativeElectionMetricValid)return kIOReturnNotReady;
    metric=awdlNativeElectionValid?awdlNativeElectionSelfMetric:awdlNativeElectionMetricRequested;
    overrideMetric=awdlNativeElectionMetricOverride;
    return kIOReturnSuccess;
}
