// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "IbssIwmCommands.hpp"
#include "MccChannelProfile.hpp"
namespace ItlIbss {
// Values pinned to the real net80211/NVM definitions by runtime static_asserts.
struct ProfileChannel5 {uint32_t channel=0,frequency=0,flags=0,nvm=0;};
constexpr uint32_t Profile5Required=0x00010100u;
constexpr uint32_t Profile5Denied=0x00280200u; // NOADHOC, DFS, PASSIVE.
constexpr uint32_t ProfileNvmRequired=0x000bu; // VALID, IBSS, ACTIVE.
constexpr uint32_t ProfileNvmDenied=0x0090u; // RADAR, DFS.
inline bool profile5Eligible(const ProfileChannel5 &c) {
    return (c.channel==44 || c.channel==149) && c.frequency==5000+c.channel*5 &&
        (c.flags & Profile5Required)==Profile5Required && !(c.flags & Profile5Denied) &&
        (c.nvm & ProfileNvmRequired)==ProfileNvmRequired && !(c.nvm & ProfileNvmDenied);
}
inline uint32_t chooseProfile5(const ProfileChannel5 *c,size_t n,bool nvmObserved) {
    if(!c || !nvmObserved || n>2)return 0;
    for(size_t i=0;i<n;++i)if(profile5Eligible(c[i]))return c[i].channel;
    return 0;
}

// Explicit 20 MHz command; never consult the ordinary AP node's channel width.
inline bool buildPhy20(const IwmSession &s, uint8_t rxAntennas, uint8_t txAntennas,
                       uint8_t chains, bool uhb, uint8_t *out, size_t cap, size_t &written) {
    written = 0;
    size_t bytes = uhb ? sizeof(iwm_phy_context_cmd_uhb) : sizeof(iwm_phy_context_cmd);
    if (!out || cap < bytes || !s.resources.valid() || !valid(s.mac) ||
        !rxAntennas || !txAntennas || (rxAntennas & ~7u) || (txAntennas & ~7u) ||
        !chains || chains > 2) return false;
    unsigned available = 0;
    for (unsigned i = 0; i < 3; ++i) available += (rxAntennas >> i) & 1;
    if (chains > available) return false;
    for (size_t i = 0; i < bytes; ++i) out[i] = 0;
    le32(out,uint32_t(s.resources.phy) | (uint32_t(s.phyColor) << 8));
    le32(out+4,2); // MODIFY: all three contexts already created by init_hw.
    const uint8_t band = s.mac.channel == 6 ? 1 : 0;
    if (uhb) { le32(out+16,s.mac.channel); out[20] = band; }
    else { out[16] = band; out[17] = s.mac.channel; }
    size_t tail = uhb ? 24 : 20;
    le32(out+tail,txAntennas);
    le32(out+tail+4,uint32_t(rxAntennas) << 1 | uint32_t(chains) << 10 | uint32_t(chains) << 12);
    written = bytes;
    return true;
}
inline uint32_t chooseProfile5WithMcc(const ProfileChannel5 *c,size_t n,bool nvmObserved,
                                       const MccChannelProfile &mcc,uint32_t generation) {
    if(!c || !nvmObserved || n>2)return 0;
    for(size_t i=0;i<n;++i)if(profile5Eligible(c[i]) && mcc.permitsActiveIbss(c[i].channel,generation))return c[i].channel;
    return 0;
}
struct BeaconCapabilities {
    uint16_t htCaps;
    uint8_t ampdu, mcs[10], txMcs;
};
// Restricted test beacon, not an AWDL action advertisement or service claim.
// HT40/SGI40/DSSS-CCK40 are removed; MCS bytes come from the actual local NIC.
inline bool buildBeacon20(const MacInput &mac, const BeaconCapabilities &c,
                          uint8_t *out, size_t cap, size_t &written) {
    written = 0;
    const size_t bytes = (mac.qos ? 146 : 120) - (mac.channel == 6 ? 0 : 3);
    if (!valid(mac) || !out || cap < bytes || !c.mcs[0] || (c.mcs[4] & 1)) return false;
    for (size_t i = 0; i < bytes; ++i) out[i] = 0;
    out[0] = 0x80;
    for (unsigned i = 0; i < 6; ++i) {
        out[4+i] = 255; out[10+i] = mac.source[i]; out[16+i] = mac.bssid[i];
    }
    le16(out+32,10000); // IBSS, short preamble/slot as declared by the MAC.
    le16(out+34,uint16_t(2 | (mac.shortPreamble ? 0x20 : 0) | (mac.shortSlot ? 0x400 : 0)));
    size_t p = 36;
    const uint8_t ssid[] = {'i','t','l','-','a','w','d','l','-','t','e','s','t'};
    out[p++] = 0; out[p++] = sizeof(ssid);
    for (auto b : ssid) out[p++] = b;
    out[p++] = 1; out[p++] = 8;
    const uint8_t rates24[8] = {0x82,0x84,0x8b,0x96,12,24,48,108};
    const uint8_t rates5[8] = {0x8c,18,0x98,36,0xb0,72,96,108};
    for (unsigned i=0;i<8;++i) out[p++] = mac.channel == 6 ? rates24[i] : rates5[i];
    if(mac.channel == 6){out[p++] = 3; out[p++] = 1; out[p++] = mac.channel;}
    out[p++] = 6; out[p++] = 2; p += 2; // ATIM window=0.
    out[p++] = 45; out[p++] = 26;
    const uint16_t ht = uint16_t(c.htCaps & ~(2u | 0x40u | 0x1000u));
    le16(out+p,ht); out[p+2] = c.ampdu;
    for (unsigned i = 0; i < 10; ++i) out[p+3+i] = c.mcs[i];
    out[p+15] = c.txMcs; p += 26;
    out[p++] = 61; out[p++] = 22; out[p] = mac.channel; p += 22;
    if (mac.qos) {
        out[p++] = 221; out[p++] = 24;
        const uint8_t wmm[] = {0,0x50,0xf2,2,1,1,0,0};
        for (auto b : wmm) out[p++] = b;
        // WMM ACI order BE/BK/VI/VO differs from firmware BK/BE/VI/VO.
        const unsigned order[4] = {1,0,2,3};
        for (unsigned aci = 0; aci < 4; ++aci) {
            const auto &a = mac.ac[order[aci]];
            unsigned ecwMin = 16, ecwMax = 16;
            for (unsigned e = 0; e < 16; ++e) {
                if (((1u << e)-1) == a.cwMin) ecwMin = e;
                if (((1u << e)-1) == a.cwMax) ecwMax = e;
            }
            if (ecwMin > 15 || ecwMax > 15 || a.aifs > 15 || a.txopUs % 32) return false;
            out[p++] = uint8_t((aci << 5) | a.aifs);
            out[p++] = uint8_t(ecwMin | (ecwMax << 4));
            le16(out+p,uint16_t(a.txopUs/32)); p += 2;
        }
    }
    if (p > cap || !validBeacon(mac,out,p)) return false;
    written = p; return true;
}
template<class T> int configureProfile(T &t, IwmSession &s,
                                      const uint8_t *beacon, size_t length,
                                      uint8_t rx, uint8_t tx, uint8_t chains) {
    if (!s.resources.valid() || !valid(s.mac) || s.needsReset || s.commandInFlight ||
        s.prerequisitesConfirmed || !validBeacon(s.mac,beacon,length)) return EINVAL;
    uint8_t phy[sizeof(iwm_phy_context_cmd_uhb)]; size_t phyBytes = 0;
    if (!buildPhy20(s,rx,tx,chains,t.uhb(),phy,sizeof(phy),phyBytes)) return EINVAL;
    int e = t.phy(phy,phyBytes);
    if (e) { s.needsReset = true; return e; }
    s.prerequisitesConfirmed = true;
    const Action steps[] = {Action::UploadBeacon,Action::AddMac,Action::AddBinding,
        Action::AddMcast,Action::AddBcast,Action::UpdatePower,Action::GrantQuota};
    for (auto step : steps) {
        e = t.command(step,beacon,length);
        if (e) { s.needsReset = true; return e; }
    }
    return 0; // Config commands completed, never an air/RX/ACK verdict.
}
// Passive comparison only. Potential ownership is retained until the existing physical stop/reset.
template<class T> int configureListenerProfile(T &t,IwmSession &s,uint8_t rx,uint8_t tx,uint8_t chains){
    if(!s.resources.valid()||!valid(s.mac)||s.needsReset||s.commandInFlight||s.prerequisitesConfirmed)return EINVAL;
    uint8_t phy[sizeof(iwm_phy_context_cmd_uhb)];size_t n=0;
    if(!buildPhy20(s,rx,tx,chains,t.uhb(),phy,sizeof(phy),n))return EINVAL;
    int error=t.phy(phy,n);if(error){s.needsReset=true;return error;}s.prerequisitesConfirmed=true;
    const Action steps[]={Action::AddListenerMac,Action::AddBinding,Action::ListenerPower,Action::GrantQuota,Action::AddSniffer};
    // Power/quota precede sniffer-STA attachment, as in Linux assign_vif_chanctx.
    for(auto step:steps){error=t.command(step,nullptr,0);if(error){s.needsReset=true;return error;}}
    return 0;
}
} // namespace ItlIbss
