// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "ResourceWire.hpp"
// Numeric append-only observation at the native PDU boundary. Does not reserve
// resources, gate ordinary operations, send commands, read hardware or allocate.
uint32_t ItlIwm::awdlP2pResourceSubmit(uint32_t id,uint32_t flags,uint16_t length,const void *data,bool wantsStatus) {
    if(!awdlProbeCounters || (id!=IWM_PHY_CONTEXT_CMD && id!=IWM_MAC_CONTEXT_CMD && id!=IWM_BINDING_CONTEXT_CMD))return 0;
    auto &d=awdlProbeCounters->ibss;const auto now=itlAwdlRocNowNs();
    if(!d.get(ItlIbss::ExperimentObservation::PassiveTrace) || !d.lifecycleCaptures(now))return 0;
    using D=ItlIbss::ExperimentObservation;using H=ItlIbss::ExperimentRows;
    ItlP2p::ResourceCommand c;uint32_t detail=0;
    const bool decoded=ItlP2p::decodeResourceWire(id,data,length,c,detail);
    const bool gated=getMainWorkLoop()->inGate();
    d.set(D::ResourceTraceVersion,1);d.add(D::ResourceTraceSubmitted);
    if(!decoded)d.add(D::ResourceTraceDecodeErrors);
    if(!gated)d.add(D::ResourceTraceOffGate);
    const auto token=d.nextCommand(); // Zero is never a valid pair identifier.
    if(!token){d.add(D::ResourceTraceDecodeErrors);return 0;}
    H::Row row;const auto us=now/1000;
    row.v[H::Kind]=22;row.v[H::Code]=id;row.v[H::Gp2]=token;
    row.v[H::HostUsLow]=uint32_t(us);row.v[H::HostUsHigh]=uint32_t(us>>32);
    row.v[H::Generation]=uint32_t(com.sc_generation);row.v[H::Length]=length;
    row.v[H::Status]=(decoded?1u:0u)|(gated?2u:0u)|((flags&IWM_CMD_ASYNC)?4u:0u)|(wantsStatus?8u:0u);
    row.v[H::Flags]=c.action;row.v[H::MacContext]=c.context;
    if(c.kind==ItlP2p::ResourceKind::Phy){row.v[H::Channel]=c.channel;row.v[H::Detail0]=detail;}
    else if(c.kind==ItlP2p::ResourceKind::Mac){row.v[H::Valid]=c.role;row.v[H::Detail0]=c.tsf;row.v[H::Detail1]=detail;}
    else {row.v[H::Channel]=c.phy;row.v[H::Detail0]=c.macs[0];row.v[H::Detail1]=c.macs[1];row.v[H::Valid]=c.macs[2];row.v[H::FrameControl]=c.lmac;}
    d.control.append(row);return token;
}
void ItlIwm::awdlP2pResourceReturn(uint32_t token,uint32_t id,uint32_t flags,int error,bool statusDecoded,uint32_t status) {
    if(!token || !awdlProbeCounters)return;
    auto &d=awdlProbeCounters->ibss;const auto now=itlAwdlRocNowNs();
    // A return outside the bounded window leaves an unfinished pair, not an
    // inferred success. Do not extend the source lease for a synchronous wait.
    if(!d.lifecycleCaptures(now))return;
    using D=ItlIbss::ExperimentObservation;using H=ItlIbss::ExperimentRows;
    const bool gated=getMainWorkLoop()->inGate();
    d.add(D::ResourceTraceReturned);if(!gated)d.add(D::ResourceTraceOffGate);
    H::Row row;const auto us=now/1000;
    row.v[H::Kind]=23;row.v[H::Code]=id;row.v[H::Gp2]=token;
    row.v[H::HostUsLow]=uint32_t(us);row.v[H::HostUsHigh]=uint32_t(us>>32);
    row.v[H::Generation]=uint32_t(com.sc_generation);row.v[H::Result]=uint32_t(error);
    row.v[H::Flags]=flags;row.v[H::Valid]=(statusDecoded?1u:0u)|(gated?2u:0u);
    row.v[H::Status]=statusDecoded?status:UINT32_MAX;d.control.append(row);
}
