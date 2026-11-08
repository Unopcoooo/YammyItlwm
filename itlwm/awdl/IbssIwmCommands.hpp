// SPDX-License-Identifier: GPL-2.0-or-later
// Requires the real IWM command declarations before inclusion. The host test
// extracts those SAME declarations rather than maintaining substitute structs.
#pragma once
#include "IbssCommands.hpp"
#include "IbssLifecycle.hpp"
#include <sys/errno.h>

namespace ItlIbss {
inline bool profileActionAllowed(Action a,bool listener){
    const bool specific=a==Action::AddListenerMac||a==Action::AddSniffer||a==Action::ListenerPower;
    return listener?(specific||a==Action::AddBinding||a==Action::GrantQuota):!specific;
}
struct IwmSession {
    Reservation resources = {};
    MacInput mac = {};
    uint32_t generation = 0;
    uint8_t phyColor = 0;
    // Future executor must prove exclusive STA quiescence, PHY HT20 and a
    // reservation in the driver allocator before setting this. No caller yet.
    bool prerequisitesConfirmed = false;
    bool beacon = false, macAdded = false, binding = false;
    bool mcastQueue = false, bcastQueue = false, mcast = false, bcast = false;
    bool needsReset = false;
    bool commandInFlight = false;
    bool powerAdjusted = false, quotaConfigured = false;
    bool valid() const {
        return prerequisitesConfirmed && generation && resources.valid() &&
            ItlIbss::valid(mac) && resources.mac == mac.id && resources.tsf == mac.tsf;
    }
};
// All actual send failures are indeterminate: do not roll back ownership by
// pretending a failed/partial hardware command had no side effects.
template<class Transport> class IwmCommands {
    Transport &t;
    IwmSession &s;
    int sent(int error) { if (error) s.needsReset = true; return error; }
    int station(bool multicast, bool add) {
        bool &queue = multicast ? s.mcastQueue : s.bcastQueue;
        bool &stationAdded = multicast ? s.mcast : s.bcast;
        const uint8_t station = multicast ? s.resources.mcastSta : s.resources.bcastSta;
        const uint8_t qid = multicast ? 3 : 9, tid = multicast ? 0 : 8;
        if (add) {
            if (!s.binding || stationAdded || queue || (!multicast && !s.mcast)) return EBUSY;
            // Record potential ownership BEFORE enabling queue/register writes.
            queue = true;
            int e = t.enableQueue(station,qid,multicast ? 1 : 3,tid);
            if (e) return sent(e);
            iwm_add_sta_cmd cmd = {};
            cmd.sta_id = station; cmd.station_type = multicast ? 2 : 1;
            ItlIbss::le16(reinterpret_cast<uint8_t *>(&cmd.tid_disable_tx),0xffff);
            ItlIbss::le32(reinterpret_cast<uint8_t *>(&cmd.mac_id_n_color),
                         uint32_t(s.mac.id) | (uint32_t(s.mac.color) << 8));
            ItlIbss::le32(reinterpret_cast<uint8_t *>(&cmd.tfd_queue_msk),1u << qid);
            if (multicast) cmd.addr[0] = 3;
            else for (unsigned i = 0; i < 6; ++i) cmd.addr[i] = s.mac.bssid[i];
            uint32_t status = 1;
            stationAdded = true; // Retain potential ownership if reply unknown.
            e = t.status(0x18,&cmd,sizeof(cmd),status);
            if (!e && (status & 255) != 1) e = EIO;
            return sent(e);
        }
        if (!stationAdded || !queue || (multicast && s.bcast)) return EBUSY;
        int e = t.flush(1u << qid); if (e) return sent(e);
        e = t.disableQueue(station,qid,tid); if (e) return sent(e);
        queue = false;
        iwm_rm_sta_cmd cmd = {}; cmd.sta_id = station;
        e = t.pdu(0x19,&cmd,sizeof(cmd)); if (e) return sent(e);
        stationAdded = false;
        return 0;
    }
public:
    IwmCommands(Transport &transport, IwmSession &session) : t(transport), s(session) {}
    int execute(Action action, const uint8_t *frame = nullptr, size_t length = 0, bool dataRxDiagnostic = false) {
        if (!s.valid()) return EINVAL;
        if (s.needsReset) return EIO;
        if (s.commandInFlight) return EBUSY;
        if (!t.ready(s)) return EBUSY;
        // IWM synchronous waits can temporarily release the workloop gate.
        // Reject reentrant submission on this session until this call returns.
        struct InFlight {
            bool &flag;
            explicit InFlight(bool &f) : flag(f) { flag = true; }
            ~InFlight() { flag = false; }
        } inFlight(s.commandInFlight);
        switch (action) {
        case Action::UploadBeacon: {
            if (s.beacon || s.macAdded) return EBUSY;
            uint8_t buffer[2336]; size_t written = 0;
            if (!buildBeaconV9(s.mac,true,true,frame,length,buffer,sizeof(buffer),written))
                return EINVAL;
            // Native Linux beacon upload uses CMD_ASYNC. IWM copies payload
            // into persistent command-ring storage before returning. This
            // return is host submission only, not firmware/air acceptance.
            s.beacon = true;
            return sent(t.asyncPdu(0x91,buffer,written));
        }
        case Action::AddListenerMac: {
            if(s.beacon||s.macAdded||s.binding||s.mcast||s.bcast)return EBUSY;
            uint8_t bytes[MacBytes];if(!buildListenerMac(s.mac,bytes,sizeof(bytes)))return EINVAL;
            s.macAdded=true;return sent(t.pdu(0x28,bytes,sizeof(bytes)));
        }
        case Action::AddSniffer: {
            if(!s.macAdded||!s.binding||s.beacon||s.mcast||s.bcast||s.mcastQueue||s.bcastQueue)return EBUSY;
            // DQA monitor queue2 is inventoried separately in the fresh-image native adapter.
            s.mcastQueue=true;int e=t.enableQueue(s.resources.mcastSta,2,1,8);if(e)return sent(e);
            iwm_add_sta_cmd cmd={};cmd.sta_id=s.resources.mcastSta;cmd.station_type=1;
            le16(reinterpret_cast<uint8_t *>(&cmd.tid_disable_tx),0xffff);
            le32(reinterpret_cast<uint8_t *>(&cmd.mac_id_n_color),uint32_t(s.mac.id)|(uint32_t(s.mac.color)<<8));
            le32(reinterpret_cast<uint8_t *>(&cmd.tfd_queue_msk),1u<<2);
            uint32_t status=0;s.mcast=true;e=t.status(0x18,&cmd,sizeof(cmd),status);
            if(!e&&(status&255)!=1)e=EIO;return sent(e);
        }
        case Action::ListenerPower: {
            if(!s.macAdded||!s.binding||s.mcast||s.mcastQueue||s.bcast||s.beacon||s.powerAdjusted)return EBUSY;
            iwm_device_power_cmd cmd={};s.powerAdjusted=true;return sent(t.pdu(0x77,&cmd,sizeof(cmd)));
        }
        case Action::AddMac: {
            if (!s.beacon || s.macAdded) return EBUSY;
            uint8_t buffer[MacBytes];
            if (!buildMac(s.mac,buffer,sizeof(buffer),dataRxDiagnostic)) return EINVAL;
            s.macAdded = true;
            return sent(t.pdu(0x28,buffer,sizeof(buffer)));
        }
        case Action::AddBinding:
        case Action::RemoveBinding: {
            bool add = action == Action::AddBinding;
            if (!s.macAdded || s.binding == add || (!add && (s.mcast || s.bcast))) return EBUSY;
            iwm_binding_cmd cmd = {};
            const uint32_t phy = uint32_t(s.resources.phy) | (uint32_t(s.phyColor) << 8);
            le32(reinterpret_cast<uint8_t *>(&cmd.id_and_color),phy);
            le32(reinterpret_cast<uint8_t *>(&cmd.phy),phy);
            le32(reinterpret_cast<uint8_t *>(&cmd.action),add ? 1 : 3);
            for (unsigned i = 0; i < 3; ++i)
                le32(reinterpret_cast<uint8_t *>(&cmd.macs[i]),i ? UINT32_MAX :
                     uint32_t(s.mac.id) | (uint32_t(s.mac.color) << 8));
            uint32_t status = 0;
            // Keep ownership until a REMOVE really succeeds.
            if (add) s.binding = true;
            int e = t.status(0x2b,&cmd,t.bindingBytes(),status);
            if (!e && status) e = EIO;
            if (e) return sent(e);
            s.binding = add;
            return 0;
        }
        case Action::AddMcast: return station(true,true);
        case Action::AddBcast: return station(false,true);
        case Action::RemoveBcast: return station(false,false);
        case Action::RemoveMcast: return station(true,false);
        case Action::RemoveMac: {
            if (!s.macAdded || s.binding || s.mcast || s.bcast) return EBUSY;
            iwm_mac_ctx_cmd cmd = {};
            le32(reinterpret_cast<uint8_t *>(&cmd.id_and_color),
                 uint32_t(s.mac.id) | (uint32_t(s.mac.color) << 8));
            le32(reinterpret_cast<uint8_t *>(&cmd.action),3);
            int e = t.pdu(0x28,&cmd,sizeof(cmd)); if (e) return sent(e);
            s.macAdded = s.beacon = false;
            return 0;
        }
        case Action::UpdatePower: {
            if (!s.mcast || !s.bcast || s.powerAdjusted) return EBUSY;
            // Explicit bounded-experiment CAM choice: clear PS enable, as in
            // Linux power_update_device for ps_disabled. No BSS-only A9 table.
            iwm_device_power_cmd cmd = {};
            s.powerAdjusted = true;
            return sent(t.pdu(0x77,&cmd,sizeof(cmd)));
        }
        case Action::GrantQuota:
            if (!s.binding || !s.powerAdjusted || s.quotaConfigured) return EBUSY;
            if (!t.dynamicQuota()) return ENOTSUP;
            // Match official update_quotas early return on DYNAMIC_QUOTA.
            // This records policy selection, NOT a granted on-air time window.
            s.quotaConfigured = true; return 0;
        // No placeholders for STA quiesce/restore, PHY, power or quota.
        // They must be implemented before any executor can run this backend.
        default: return ENOTSUP;
        }
    }
};
} // namespace ItlIbss
