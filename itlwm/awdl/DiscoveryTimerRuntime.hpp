// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Separate experimental timer: do not accelerate AP calibration/rate control.
void ItlIwm::awdlDiscoveryTimerStart() {
    ItlAwdlRocLifetime lifetime(this,getMainCommandGate(),getMainWorkLoop());
    if(!getMainWorkLoop()->inGate()) {
        getMainCommandGate()->runAction([](OSObject*,void *arg,void*,void*,void*) -> IOReturn {
            static_cast<ItlIwm*>(arg)->awdlDiscoveryTimerStart();return kIOReturnSuccess;
        },this);return;
    }
    if((!awdlDiscoveryEnabled&&!awdlTemplateTxEnabled) || awdlDiscoveryTimer) return;
    auto timer=IOTimerEventSource::timerEventSource(this,awdlDiscoveryTimerAction);
    if(!timer || getMainWorkLoop()->addEventSource(timer)!=kIOReturnSuccess) {
        if(timer)timer->release();
        awdlDiscoveryEnabled=false;ItlAwdl::gEngine.modeEnabled=0;awdlRocChannel=0;
        if(awdlProbeCounters)awdlProbeCounters->setRoc(ItlAwdlProbeCounters::RocSkipReason,17);
        return; // only the experiment stops; ordinary Wi-Fi still attaches
    }
    awdlDiscoveryTimer=timer;timer->setTimeoutMS(500);
}
void ItlIwm::awdlDiscoveryTimerStop() {
    if(!awdlDiscoveryTimer)return;
    ItlAwdlRocLifetime lifetime(this,getMainCommandGate(),getMainWorkLoop());
    if(!getMainWorkLoop()->inGate()) {
        getMainCommandGate()->runAction([](OSObject*,void *arg,void*,void*,void*) -> IOReturn {
            static_cast<ItlIwm*>(arg)->awdlDiscoveryTimerStop();return kIOReturnSuccess;
        },this);return;
    }
    auto timer=awdlDiscoveryTimer;awdlDiscoveryTimer=nullptr;
    ItlAwdl::gDataPort.stop(false,ECANCELED);
    if(timer) {
        timer->cancelTimeout();getMainWorkLoop()->removeEventSource(timer);timer->release();
    }
}
void ItlIwm::awdlDiscoveryTimerAction(OSObject *owner,IOTimerEventSource *sender) {
    auto hal=static_cast<ItlIwm*>(owner);
    ItlAwdlRocLifetime lifetime(hal,hal->getMainCommandGate(),hal->getMainWorkLoop());
    sender->retain(); // detach may remove/release it while a firmware command sleeps
    ItlAwdl::gDataPort.expire(hal->com.sc_generation,itlAwdlRocNowNs(),ECANCELED);
    if(hal->awdlDiscoveryTimer==sender && (hal->awdlDiscoveryEnabled||hal->awdlTemplateTxEnabled) && ItlAwdl::gEngine.modeEnabled) {
        // Leave the first ten normal calibration callbacks as the settle period.
        // Before that, never arm a deadline during cold firmware initialization.
        if(hal->awdlRocTicks>=10)hal->awdlRocTick();
        const bool idle=hal->awdlRocSession.phase==ItlAwdl::RocPhase::Idle;
        const bool done=idle && ((hal->awdlDiscoveryEnabled&&hal->awdlDiscoveryPolicy.exhausted(hal->awdlPeerRocAttempts)) ||
            (ItlAwdl::gEngine.deadlineNs && itlAwdlRocNowNs()>=uint64_t(ItlAwdl::gEngine.deadlineNs) && (!hal->awdlTemplateTxEnabled||ItlAwdl::gTemplateRadio.validationArmed)));
        if(hal->awdlDiscoveryTimer==sender && (hal->awdlDiscoveryEnabled||hal->awdlTemplateTxEnabled) &&
            ItlAwdl::gEngine.modeEnabled && !done)
        {
            if(hal->awdlTemplateTxEnabled&&hal->awdlRocTicks>=10){
                const uint64_t now=itlAwdlRocNowNs(),wake=ItlAwdl::gTemplateRadio.nextWakeNs;
                uint32_t wait=idle?(ItlAwdl::gTemplateRadio.targetValid?500:500000):20000;
                if(wake>now)wait=uint32_t(MIN(uint64_t(20000),MAX(uint64_t(100),(wake-now)/1000)));
                sender->setTimeoutUS(wait);
            }else sender->setTimeoutMS(hal->awdlRocTicks>=10?20:500);
        }
    }
    sender->release();
}
