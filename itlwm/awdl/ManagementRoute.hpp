// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "IbssLifecycle.hpp"
namespace ItlIbss {
struct ManagementRoute {uint8_t station=0,queue=0;};
// Routing arithmetic only. Does not authorize or submit TX, reserve a queue,
// establish firmware readiness, or attest scheduler/command-station compatibility.
inline bool managementRoute(const Reservation &r,uint16_t fc,bool group,ManagementRoute &out) {
    out={};
    if(!r.valid() || (fc&0x000f))return false; // v0 management only
    const uint16_t subtype=fc&0x00f0;
    // Linux v5.4 iwl_mvm_get_ctrl_vif_queue: action is bufferable; deauth
    // and disassoc are explicitly sent on probe queue despite bufferability.
    const bool cab=subtype==0x00d0 && !(fc&0x8000) && group;
    out.station=r.bcastSta;out.queue=cab?r.mcastQueue:r.bcastQueue;return true;
}
}
