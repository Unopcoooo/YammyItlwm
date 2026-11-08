// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
namespace ItlAwdl {
// Four consecutive receives on one channel can supply three fresh advertisements.
// Firmware grants may still delay/preempt them; actual RX/timing is the verdict.
struct DiscoveryPolicy {
    uint32_t attempts=0, aligned=0;
    uint64_t nextBurstNs=0;
    bool candidate(uint64_t now, uint32_t dtimTu, uint32_t &channel, uint32_t &durationTu) const {
        channel=durationTu=0;
        if(attempts>=24 || now<nextBurstNs || dtimTu<32) return false;
        static const uint32_t channels[]={6,149,44};
        channel=channels[(attempts/4)%3];
        // Stay below one DTIM, with at least ten TU for the infrastructure link.
        durationTu=dtimTu>74 ? 64 : dtimTu-10;
        return true;
    }
    void submitted(uint64_t now, bool discovery) {
        if(!discovery) { ++aligned;return; }
        ++attempts;
        if(!(attempts%4)) nextBurstNs=now+60000000000ULL;
    }
    bool exhausted(uint32_t total) const { return total>=48 || aligned>=24; }
};
}
