// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Included by ProbeCounters after IOKit collection headers.
inline bool serializeIbssExperiment(OSDictionary *dict,const ItlIbss::ExperimentObservation &data) {
    auto d = OSDictionary::withCapacity(ItlIbss::ExperimentObservation::Count+4);
    bool ok = d && dict->setObject("IbssExperiment",d);
    for (unsigned i = 0; ok && i < ItlIbss::ExperimentObservation::Count; ++i) {
        auto n = OSNumber::withNumber(data.get(static_cast<ItlIbss::ExperimentObservation::Metric>(i)),32);
        ok = n && d->setObject(ItlIbss::ExperimentObservation::name(i),n); if (n) n->release();
    }
    auto clockVersion=OSNumber::withNumber(1,32);
    if(ok)ok=clockVersion && d->setObject("DeviceClockVersion",clockVersion);
    if(clockVersion)clockVersion->release();
    auto windowVersion=OSNumber::withNumber(2,32);
    if(ok)ok=windowVersion && d->setObject("WindowProbeVersion",windowVersion);
    if(windowVersion)windowVersion->release();
    const uint64_t times[] = {__atomic_load_n(&data.startNs,__ATOMIC_SEQ_CST),
        __atomic_load_n(&data.deadlineNs,__ATOMIC_SEQ_CST),
        __atomic_load_n(&data.lifecycleStartNs,__ATOMIC_SEQ_CST),__atomic_load_n(&data.lifecycleDeadlineNs,__ATOMIC_SEQ_CST),
        __atomic_load_n(&data.recoveryStartNs,__ATOMIC_SEQ_CST),__atomic_load_n(&data.recoveryDeadlineNs,__ATOMIC_SEQ_CST)};
    const char *timeNames[]={"StartNs","DeadlineNs","LifecycleStartNs","LifecycleDeadlineNs","RecoveryStartNs","RecoveryDeadlineNs"};
    for (unsigned i = 0; ok && i < 6; ++i) {
        auto n = OSNumber::withNumber(times[i],64);
        ok = n && d->setObject(timeNames[i],n); if (n) n->release();
    }
    const ItlIbss::ExperimentRows *rings[] = {&data.control,&data.rx};
    for (unsigned i = 0; ok && i < 2; ++i) {
        auto rows = OSArray::withCapacity(ItlIbss::ExperimentRows::Capacity);
        ok = rows && d->setObject(i ? "RxRows" : "ControlRows",rows);
        auto lost = OSNumber::withNumber(__atomic_load_n(&rings[i]->lost,__ATOMIC_SEQ_CST),32);
        if (ok) ok = lost && d->setObject(i ? "RxRowsLost" : "ControlRowsLost",lost);
        if (lost) lost->release();
        for (unsigned j = 0; ok && j < ItlIbss::ExperimentRows::Capacity; ++j) {
            ItlIbss::ExperimentRows::Row row;
            if (!rings[i]->read(j,row)) continue;
            auto r = OSDictionary::withCapacity(ItlIbss::ExperimentRows::Count); ok = r;
            for (unsigned k = 0; ok && k < ItlIbss::ExperimentRows::Count; ++k) {
                auto n = OSNumber::withNumber(row.v[k],32);
                ok = n && r->setObject(ItlIbss::ExperimentRows::name(k),n); if (n) n->release();
            }
            if (ok) ok = rows->setObject(r); if (r) r->release();
        }
        if (rows) rows->release();
    }
    if (d) d->release(); return ok;
}
