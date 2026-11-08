// SPDX-License-Identifier: GPL-2.0-or-later
// Offline candidate. No transport, device access, or activation entry point.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace ItlIbss {
constexpr size_t MacBytes = 148; // Local IWM MAC_CONTEXT_CMD_API_S_VER_1.
constexpr size_t BeaconHeaderBytes = 32;
inline void le16(uint8_t *p, uint16_t v) {
    p[0] = uint8_t(v); p[1] = uint8_t(v >> 8);
}
inline void le32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i));
}
inline uint16_t read16(const uint8_t *p) {
    return uint16_t(uint16_t(p[0]) | (uint16_t(p[1]) << 8));
}
inline bool equal6(const uint8_t *a, const uint8_t *b) {
    for (unsigned i = 0; i < 6; ++i) if (a[i] != b[i]) return false;
    return true;
}
inline bool unicast(const uint8_t *a) {
    uint8_t any = 0;
    for (unsigned i = 0; i < 6; ++i) any |= a[i];
    return any && !(a[0] & 1);
}
struct Edca {
    uint16_t cwMin, cwMax;
    uint8_t aifs, fifoMask;
    uint16_t txopUs;
};
struct MacInput {
    uint8_t id, color, tsf;
    uint8_t channel = 6; // Explicit restricted social channel; default preserves proven profile.
    uint8_t source[6], bssid[6];
    uint16_t beaconIntervalTu;
    uint8_t cckAckRates, ofdmAckRates;
    bool shortPreamble, shortSlot, qos;
    // Already in firmware AC order, not net80211 AC order.
    Edca ac[4];
};
inline unsigned beaconVersion(bool csaOffload, bool newTemplate) {
    return !csaOffload ? 6 : (newTemplate ? 9 : 7);
}
inline bool valid(const MacInput &in) {
    if (in.id >= 4 || in.tsf >= 4 || !unicast(in.source) ||
        !unicast(in.bssid) || in.beaconIntervalTu != 10000 ||
        (in.channel != 6 && in.channel != 44 && in.channel != 149) ||
        (in.channel == 6 ? !(in.cckAckRates & 1) : (in.cckAckRates != 0 || in.shortPreamble)) || !(in.ofdmAckRates & 1)) return false;
    for (const auto &a : in.ac) {
        if (a.cwMin > a.cwMax || !a.aifs || !a.fifoMask ||
            (a.fifoMask & (a.fifoMask - 1))) return false;
    }
    return true;
}
// ADD only. MODIFY/REMOVE belong to the future transport adapter. Inputs are
// explicit; no ordinary AP node, allocator identity, or EDCA is guessed here.
inline bool buildMac(const MacInput &in, uint8_t *out, size_t capacity, bool dataRxDiagnostic = false) {
    if (!out || capacity < MacBytes || !valid(in)) return false;
    for (size_t i = 0; i < MacBytes; ++i) out[i] = 0;
    le32(out, uint32_t(in.id) | (uint32_t(in.color) << 8));
    le32(out + 4, 1); // ADD
    le32(out + 8, 4); // FW_MAC_TYPE_IBSS
    le32(out + 12, in.tsf);
    for (unsigned i = 0; i < 6; ++i) {
        out[16+i] = in.source[i]; out[24+i] = in.bssid[i];
    }
    le32(out + 32, in.cckAckRates); le32(out + 36, in.ofdmAckRates);
    le32(out + 44, in.shortPreamble ? 0x20 : 0);
    le32(out + 48, in.shortSlot ? 0x10 : 0);
    // Default Linux IBSS filter; bounded data trial may also admit data.
    le32(out + 52, (1u << 6) | (1u << 12) | (1u << 2) | (dataRxDiagnostic ? 1u : 0u));
    le32(out + 56, (in.qos ? 1u : 0u) | 2u); // HT20, no HT protection
    for (unsigned i = 0; i < 4; ++i) {
        uint8_t *p = out + 60 + i * 8;
        const auto &a = in.ac[i];
        le16(p, a.cwMin); le16(p + 2, a.cwMax);
        p[4] = a.aifs; p[5] = a.fifoMask; le16(p + 6, a.txopUs);
    }
    // Fifth AC, ignored beacon time/TSF and reserved reciprocal stay zero.
    le32(out + 112, in.beaconIntervalTu);
    le32(out + 120, in.id);
    return true;
}
// Linux listener role: no IBSS union or beacon template; FCS is retained for raw diagnostics.
inline bool buildListenerMac(const MacInput &in,uint8_t *out,size_t cap){
    uint8_t bytes[MacBytes];if(!out||cap<MacBytes||!buildMac(in,bytes,sizeof(bytes)))return false;
    le32(bytes+8,2);le32(bytes+52,0x1847);
    for(unsigned i=0;i<6;++i)bytes[24+i]=255;
    for(size_t i=104;i<MacBytes;++i)bytes[i]=0;
    for(size_t i=0;i<MacBytes;++i)out[i]=bytes[i];return true;
}
// Restricted social-channel profile: 6/44/149, HT20, no CSA/TIM, no FCS.
// Full beacon/HT capability construction remains the adapter's responsibility.
inline bool validBeacon(const MacInput &in, const uint8_t *f, size_t n) {
    if (!valid(in) || !f || n < 36 || n > 2304 || f[0] != 0x80 || f[1] != 0)
        return false;
    const uint8_t broadcast[6] = {255,255,255,255,255,255};
    if (!equal6(f + 4, broadcast) || !equal6(f + 10, in.source) ||
        !equal6(f + 16, in.bssid) || read16(f + 32) != in.beaconIntervalTu ||
        (read16(f + 34) & 3) != 2) return false;
    bool ds = false, htCap = false, htOp = false, ssid = false, rates = false, ibss = false;
    for (size_t p = 36; p < n;) {
        if (n - p < 2 || size_t(f[p+1]) > n - p - 2) return false;
        uint8_t id = f[p], len = f[p+1]; const uint8_t *v = f + p + 2;
        if (id == 0) { if (ssid || len > 32) return false; ssid = true; }
        if (id == 1) {
            if (rates || !len || len > 8) return false;
            if(in.channel != 6) {
                const uint8_t expected[8]={0x8c,18,0x98,36,0xb0,72,96,108};
                if(len!=8)return false;
                for(unsigned i=0;i<8;++i)if(v[i]!=expected[i])return false;
            }
            rates = true;
        }
        if (id == 3) { if (ds || len != 1 || v[0] != in.channel) return false; ds = true; }
        if (id == 6) {
            if (ibss || len != 2 || read16(v) != 0) return false;
            ibss = true; // Initial experiment uses no ATIM power-save window.
        }
        if (id == 45) {
            if (htCap || len != 26 || (read16(v) & 2)) return false;
            htCap = true;
        }
        if (id == 61) {
            if (htOp || len != 22 || v[0] != in.channel || (v[1] & 7)) return false;
            htOp = true;
        }
        if (id == 5 || id == 37 || id == 60) return false;
        p += 2 + len;
    }
    return (in.channel == 6 ? ds : !ds) && htCap && htOp && ssid && rates && ibss;
}
inline bool buildBeaconV9(const MacInput &in, bool csaOffload, bool newTemplate,
                          const uint8_t *frame, size_t length,
                          uint8_t *out, size_t capacity, size_t &written) {
    written = 0;
    if (!out || beaconVersion(csaOffload, newTemplate) != 9 ||
        !validBeacon(in, frame, length) || capacity < BeaconHeaderBytes + length)
        return false;
    // The caller must provide nonoverlapping frame/output storage.
    const uintptr_t a = reinterpret_cast<uintptr_t>(out);
    const uintptr_t b = reinterpret_cast<uintptr_t>(frame);
    if ((a <= b && b - a < BeaconHeaderBytes + length) ||
        (b < a && a - b < length)) return false;
    for (size_t i = 0; i < BeaconHeaderBytes; ++i) out[i] = 0;
    le16(out, uint16_t(length));
    le16(out + 2, in.channel == 6 ? 0x10a : 13); // 1 Mbps CCK / 6 Mbps OFDM.
    le32(out + 12, in.id);
    for (size_t i = 0; i < length; ++i) out[BeaconHeaderBytes+i] = frame[i];
    written = BeaconHeaderBytes + length;
    return true;
}
} // namespace ItlIbss
