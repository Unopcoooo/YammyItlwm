// SPDX-License-Identifier: GPL-2.0-or-later
// Wrap the system's AWDL TLV payload with the header and TLVs normally owned
// by the firmware. No raw system payload is transmitted as a full 802.11 frame.
#pragma once
#include "ActionBuilder.hpp"
#include "SyncTemplate.hpp"
#include "TemplateRuntime.hpp"
namespace ItlAwdl {
struct SystemTemplateInfo {
    uint8_t master[6], sync[6];
    uint32_t masterMetric, selfMetric;
    bool service, election, datapath, version;
};
inline bool parseSystemTemplate(const uint8_t *frame, uint32_t length, SystemTemplateInfo &out) {
    out = {};
    if(!frame||!length||length>kSyncTemplateCapacity)return false;
    uint32_t offset = 0;
    while (offset < length) {
        if (length - offset < 3) return false;
        const uint8_t type = frame[offset];
        const uint32_t size = uint32_t(frame[offset + 1]) | (uint32_t(frame[offset + 2]) << 8);
        offset += 3;
        if (size > length - offset) return false;
        const uint8_t *body = frame + offset;
        if (type == 24) {
            if (out.election || size != 40 || (body[0] & 1) || (body[6] & 1)) return false;
            memcpy(out.master, body, 6); memcpy(out.sync, body + 6, 6);
            out.masterMetric = actionRead32(body + 20);
            out.selfMetric = actionRead32(body + 24);
            out.election = true;
        } else if (type == 6) out.service = true;
        else if (type == 12) out.datapath = true;
        else if (type == 21) out.version = true;
        offset += size;
    }
    return offset == length && out.service && out.election && out.datapath && out.version;
}
struct TemplateFrameInput {
    TemplateSyncConfig sync;
    TemplateService service;
    TemplateClock clock;
    uint32_t generation=0,revision=0,gp2=0,startGp2=0,targetGp2=0;
    uint64_t hostNs=0,leaseEndNs=0;
};
inline bool templateFrameInputValid(const TemplateFrameInput &i) {
    return templateScheduleValid(i.sync,6)&&templateIdentityValid(i.service,i.hostNs)&&
        i.clock.valid(i.generation,i.revision,i.gp2,i.hostNs)&&
        uint32_t(i.startGp2-i.targetGp2)<=2000&&int32_t(i.gp2-i.startGp2)>=0&&
        uint32_t(i.gp2-i.targetGp2)+2000<=uint32_t(i.sync.period)*i.sync.presence*1024u&&
        i.leaseEndNs>i.hostNs&&i.leaseEndNs-i.hostNs>=
            (uint64_t(i.sync.period)*i.sync.presence*16*1024+2000)*1000;
}
inline void encodeSystemChannelSequence(uint8_t *p,const TemplateSyncConfig &s) {
    p[0]=15;p[1]=s.encoding;p[2]=0;p[3]=uint8_t(s.steps-1);
    actionWrite16(p+4,0xffff);
    for(unsigned i=0;i<16;++i)actionWrite16(p+6+2*i,s.channels[i]);
}
constexpr size_t kTemplateActionPrefix = 184;
inline bool buildOwnedPrefix(const SystemTemplateInfo &info,const TemplateFrameInput &i,
                             uint16_t sequence,uint8_t subtype,uint8_t *out) {
    // shortcut: following a remote master needs measured peer-clock conversion;
    // until that exists, refuse that role rather than transmit under its address.
    if(!templateFrameInputValid(i)||sequence>4095||
       memcmp(info.master,i.service.source,6)||memcmp(info.sync,i.service.source,6))return false;
    for(size_t n=0;n<kTemplateActionPrefix;++n)out[n]=0;
    out[0]=0xd0;
    const uint8_t bssid[6]={0,0x25,0,0xff,0x94,0x73};
    for(unsigned n=0;n<6;++n){out[4+n]=255;out[10+n]=i.service.source[n];out[16+n]=bssid[n];}
    actionWrite16(out+22,uint16_t(sequence<<4));
    out[24]=127;out[26]=0x17;out[27]=0xf2;out[28]=8;out[29]=0x10;out[30]=subtype;
    actionWrite32(out+32,i.gp2);actionWrite32(out+36,i.gp2);
    out[40]=4;actionWrite16(out+41,73);uint8_t *s=out+43;
    const auto &c=i.sync;
    s[0]=s[3]=6;s[4]=2; // two TU tolerance; admission rejects START later than 2ms
    const uint16_t down=i.clock.down(i.gp2,c);
    const uint16_t aw=i.clock.aw(i.gp2,c);
    actionWrite16(s+1,down);actionWrite16(s+5,c.period);actionWrite16(s+7,c.afPeriod);
    actionWrite16(s+9,0x1800);actionWrite16(s+11,c.extension);actionWrite16(s+13,c.length);
    const unsigned spent=unsigned(c.period)*c.presence-down;
    actionWrite16(s+15,spent<c.length?uint16_t(c.length-spent):0);
    s[17]=s[18]=s[19]=s[20]=uint8_t(c.presence-1);
    memcpy(s+21,info.master,6);s[27]=c.presence;
    actionWrite16(s+29,aw);actionWrite16(s+31,aw);encodeSystemChannelSequence(s+33,c);
    out[116]=18;actionWrite16(out+117,41);encodeSystemChannelSequence(out+119,c);
    out[160]=5;actionWrite16(out+161,21);uint8_t *one=out+163;
    memcpy(one+5,info.master,6);actionWrite32(one+11,info.masterMetric);actionWrite32(one+15,info.selfMetric);
    return true;
}
inline bool buildTemplateAction(const uint8_t *raw,uint32_t rawLength,const TemplateFrameInput &i,
                                uint16_t sequence,uint8_t *out,size_t capacity,size_t &used) {
    used=0;SystemTemplateInfo info;
    if(!out||capacity<kTemplateActionPrefix+rawLength||!parseSystemTemplate(raw,rawLength,info)||
       !buildOwnedPrefix(info,i,sequence,0,out))return false;
    memcpy(out+kTemplateActionPrefix,raw,rawLength);used=kTemplateActionPrefix+rawLength;return true;
}
inline bool writeLabel(uint8_t *&p, uint8_t *end, const char *text) {
    const size_t n = text ? strlen(text) : 0;
    if (!n || n > 63 || size_t(end - p) < n + 1) return false;
    *p++ = uint8_t(n); memcpy(p, text, n); p += n; return true;
}
inline bool writeServiceRecord(uint8_t *&p, uint8_t *end,
                               const uint8_t *name, size_t nameLength, uint8_t type,
                               const uint8_t *rdata, size_t rdataLength) {
    if (!nameLength || nameLength > 0xff || rdataLength > 0xffff ||
        size_t(end - p) < 7 + nameLength + rdataLength) return false;
    actionWrite16(p, uint16_t(nameLength + 1)); p += 2;
    memcpy(p, name, nameLength); p += nameLength;
    *p++ = type; actionWrite16(p, uint16_t(rdataLength)); p += 2;
    *p++ = 0; *p++ = 0; memcpy(p, rdata, rdataLength); p += rdataLength;
    return true;
}
inline bool appendSystemTlv(const uint8_t *raw, uint32_t rawLength, uint8_t wanted,
                            uint8_t *&p, uint8_t *end) {
    uint32_t offset = 0;
    while (offset < rawLength) {
        if (rawLength - offset < 3) return false;
        const uint32_t size = uint32_t(raw[offset + 1]) | (uint32_t(raw[offset + 2]) << 8);
        if (size > rawLength - offset - 3) return false;
        if (raw[offset] == wanted) {
            const size_t total = 3 + size;
            if (size_t(end - p) < total) return false;
            memcpy(p, raw + offset, total); p += total;
            return true;
        }
        offset += 3 + size;
    }
    return false;
}
inline bool buildServiceMifAction(const uint8_t *raw, uint32_t rawLength, const TemplateFrameInput &input,
                                  uint16_t sequence, uint8_t *out, size_t capacity,
                                  size_t &used) {
    used = 0;
    SystemTemplateInfo info;
    if (!out || capacity < 420 || !templateServiceValid(input.service,input.hostNs) || !parseSystemTemplate(raw, rawLength, info))
        return false;
    if (!buildOwnedPrefix(info, input, sequence, 3, out)) return false;
    uint8_t *p = out + kTemplateActionPrefix;
    uint8_t *end = out + capacity;
    // Keep the system's live election state ahead of the service records.
    if (!appendSystemTlv(raw, rawLength, 24, p, end)) return false;
    if (!appendSystemTlv(raw, rawLength, 6, p, end)) return false;
    // Host name TLV, matching the live awdl0 mDNS registration.
    if (end - p < 3) return false;
    *p++ = 16;
    uint8_t *hostLength = p; p += 2;
    uint8_t *hostStart = p;
    *p++ = 3;
    if (!writeLabel(p, end, input.service.host)) return false;
    if (end - p < 2) return false;
    *p++ = 0xc0; *p++ = 0x0c;
    actionWrite16(hostLength, uint16_t(p - hostStart));
    // PTR/SRV/TXT from the current LocalOnly DNS-SD registration snapshot.
    if (end - p < 3) return false;
    *p++ = 2;
    uint8_t *recordLength = p; p += 2;
    uint8_t *recordStart = p;
    const uint8_t ptrName[] = {0xc0,0x07};
    uint8_t ptrData[66]; uint8_t *q = ptrData;
    if (!writeLabel(q, ptrData + sizeof(ptrData), input.service.instance)) return false;
    *q++ = 0xc0; *q++ = 0;
    if (!writeServiceRecord(p, end, ptrName, sizeof(ptrName), 12,
                            ptrData, size_t(q - ptrData))) return false;
    uint8_t srvName[66]; q = srvName;
    if (!writeLabel(q, srvName + sizeof(srvName), input.service.instance)) return false;
    *q++ = 0xc0; *q++ = 0x07;
    const size_t srvNameLength = size_t(q - srvName);
    uint8_t srvData[72]; q = srvData;
    *q++ = 0; *q++ = 0; *q++ = 0; *q++ = 0;
    *q++ = uint8_t(input.service.port >> 8); *q++ = uint8_t(input.service.port);
    if (!writeLabel(q, srvData + sizeof(srvData), input.service.host)) return false;
    *q++ = 0xc0; *q++ = 0x0c;
    if (!writeServiceRecord(p, end, srvName, srvNameLength, 33,
                            srvData, size_t(q - srvData))) return false;
    uint8_t txtName[66]; q = txtName;
    if (!writeLabel(q, txtName + sizeof(txtName), input.service.instance)) return false;
    *q++ = 0xc0; *q++ = 0x07;
    const size_t txtNameLength = size_t(q - txtName);
    if (!writeServiceRecord(p, end, txtName, txtNameLength, 16,
                            input.service.txt, input.service.txtLength)) return false;
    actionWrite16(recordLength, uint16_t(p - recordStart));
    const uint8_t htcap[] = {0,0,0xce,0x11,0x1b,0xff,0,0};
    if (size_t(end - p) < 3 + sizeof(htcap)) return false;
    *p++ = 7; actionWrite16(p, sizeof(htcap)); p += 2;
    memcpy(p, htcap, sizeof(htcap)); p += sizeof(htcap);
    if (!appendSystemTlv(raw, rawLength, 12, p, end) ||
        !appendSystemTlv(raw, rawLength, 21, p, end)) return false;
    used = size_t(p - out);
    return true;
}
}
