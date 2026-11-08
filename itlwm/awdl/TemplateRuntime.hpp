// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <string.h>
namespace ItlAwdl {
struct TemplateSyncConfig {
    uint16_t period=16, length=16, extension=16, afPeriod=110;
    uint8_t presence=4, encoding=0, steps=0, count=0;
    uint16_t channels[16]={};
};
struct TemplateService {
    uint8_t source[6]={};
    char instance[64]={}, host[64]={};
    uint16_t port=0, txtLength=0;
    uint8_t txt[256]={};
    uint64_t expiresNs=0;
};
struct TemplateRuntimeState {
    uint32_t lock=0, syncRevision=0;
    TemplateSyncConfig sync;
    TemplateService service;
};
extern TemplateRuntimeState gTemplateRuntime;
inline bool templateLock() {
    uint32_t expected=0;
    return __atomic_compare_exchange_n(&gTemplateRuntime.lock,&expected,1,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED);
}
inline void templateUnlock() { __atomic_store_n(&gTemplateRuntime.lock,0,__ATOMIC_RELEASE); }
inline bool templateSnapshot(TemplateSyncConfig &sync,TemplateService &service,uint32_t &revision) {
    if(!templateLock())return false;
    sync=gTemplateRuntime.sync; service=gTemplateRuntime.service; revision=gTemplateRuntime.syncRevision;
    templateUnlock();return true;
}
inline bool templateLabel(const char *s) {
    unsigned n=0;
    for(;n<64&&s[n];++n)if(static_cast<unsigned char>(s[n])<33||s[n]=='.')return false;
    return n>0&&n<64;
}
inline bool templateIdentityValid(const TemplateService &s,uint64_t now){
    uint8_t any=0;for(auto x:s.source)any|=x;
    return any&&!(s.source[0]&1)&&s.expiresNs>now&&s.expiresNs-now<=5000000000ULL;
}
inline bool selectRocNodeAddress(bool templateMode,const uint8_t physical[6],const TemplateService &identity,
                                 uint64_t now,uint8_t out[6]){
    const uint8_t *node=templateMode&&templateIdentityValid(identity,now)?identity.source:physical;
    if(!node||!out||(node[0]&1))return false;
    uint8_t any=0;for(unsigned i=0;i<6;++i)any|=node[i];if(!any)return false;
    memcpy(out,node,6);return true;
}
inline bool templateServiceValid(const TemplateService &s,uint64_t now) {
    uint8_t any=0;for(auto x:s.source)any|=x;
    if(!any||(s.source[0]&1)||!s.port||!templateLabel(s.instance)||!templateLabel(s.host)||
       s.txtLength>sizeof(s.txt)||s.expiresNs<=now||s.expiresNs-now>5000000000ULL)return false;
    unsigned p=0;
    while(p<s.txtLength){unsigned n=s.txt[p++];if(n>s.txtLength-p)return false;p+=n;}
    return true;
}
inline bool templateScheduleValid(const TemplateSyncConfig &s,uint8_t channel) {
    if(!s.period||s.period>64||!s.length||s.length>s.period||s.extension>s.period||
       !s.afPeriod||!s.presence||s.presence>4||unsigned(s.period)*s.presence>64||
       s.count!=16||s.steps!=s.presence||(s.encoding!=1&&s.encoding!=3)||
       65536u%(unsigned(s.presence)*16))return false;
    bool active=false;
    for(unsigned i=0;i<16;++i){
        const uint8_t c=s.encoding==1?uint8_t(s.channels[i]>>8):uint8_t(s.channels[i]);
        if(c&&c!=channel)return false;
        active|=c==channel;
    }
    return active;
}
// Sequoia native producer/consumer use records at +12, stride12 (not wire TLVs).
inline bool decodeNativeSequence(const uint8_t *p,unsigned n,TemplateSyncConfig &out){
    if(!p||n!=400||p[5]!=16||p[6]!=0||p[7]>3||p[8]||p[9]!=255)return false;
    TemplateSyncConfig next=out;next.count=16;next.encoding=3;next.steps=p[7]+1;
    for(unsigned i=0;i<16;++i){
        const auto r=p+12+12*i;
        uint32_t version=uint32_t(r[0])|(uint32_t(r[1])<<8)|(uint32_t(r[2])<<16)|(uint32_t(r[3])<<24);
        uint32_t channel=uint32_t(r[4])|(uint32_t(r[5])<<8)|(uint32_t(r[6])<<16)|(uint32_t(r[7])<<24);
        uint32_t flags=uint32_t(r[8])|(uint32_t(r[9])<<8)|(uint32_t(r[10])<<16)|(uint32_t(r[11])<<24);
        if(version!=1||(channel!=0&&channel!=6)||(channel==6&&flags!=8)||(channel==0&&flags))return false;
        next.channels[i]=channel?0x5106:0;
    }
    out=next;return true;
}
// Request 3TU before the boundary; firmware apply_time remains current GP2.
constexpr uint32_t kTemplateLeadUs=3*1024;
inline uint32_t templateRequestWake(uint32_t target){return target-kTemplateLeadUs;}
inline bool templateLeaseAdmits(uint64_t now,uint64_t end){return end>now&&end-now>=200000000ULL;}
struct TemplateClock {
    uint32_t generation=0,revision=0,epochGp2=0;
    uint16_t epochAw=0;
    uint64_t epochHostNs=0;
    void reset(){*this={};}
    bool initialize(uint32_t gen,uint32_t rev,uint32_t gp2,uint64_t host,const TemplateSyncConfig &s){
        if(!gen||!rev||!templateScheduleValid(s,6))return false;
        const uint32_t period=uint32_t(s.period)*s.presence*1024u;
        const uint32_t phase=gp2%period;
        if(host<uint64_t(phase)*1000)return false;
        generation=gen;revision=rev;epochGp2=gp2-phase;
        epochHostNs=host-uint64_t(phase)*1000;
        epochAw=uint16_t((gp2/period)*s.presence);return true;
    }
    bool valid(uint32_t gen,uint32_t rev,uint32_t gp2,uint64_t host) const {
        if(!generation||generation!=gen||revision!=rev||host<epochHostNs)return false;
        uint64_t age=(host-epochHostNs)/1000;
        uint32_t elapsed=gp2-epochGp2;
        return age<=310000000&&elapsed<=310000000&&
            (age>elapsed?age-elapsed:elapsed-age)<=2048;
    }
    uint16_t aw(uint32_t gp2,const TemplateSyncConfig&s)const{
        return uint16_t(epochAw+uint32_t(gp2-epochGp2)/(uint32_t(s.period)*1024));
    }
    uint16_t down(uint32_t gp2,const TemplateSyncConfig&s)const{
        return uint16_t(unsigned(s.period)*s.presence-(uint32_t(gp2-epochGp2)/1024)%(unsigned(s.period)*s.presence));
    }
    bool target(const TemplateSyncConfig&s,uint32_t now,uint32_t &targetGp2,uint16_t &targetAw) const {
        const uint32_t eaw=uint32_t(s.period)*s.presence*1024u;
        uint32_t index=uint32_t(now-epochGp2)/eaw;
        for(unsigned n=0;n<=16;++n,++index){
            uint16_t a=uint16_t(epochAw+index*s.presence);
            unsigned slot=(a/s.presence)%16;
            uint8_t c=s.encoding==1?uint8_t(s.channels[slot]>>8):uint8_t(s.channels[slot]);
            uint32_t t=epochGp2+index*eaw;
            if(c==6&&int32_t(now-t)<=2000){targetGp2=t;targetAw=a;return true;}
        }
        return false;
    }
};
struct TemplateRadioState {
    TemplateClock clock;
    uint8_t rocSource[6]={};uint32_t rocSourceGeneration=0;bool rocSourceValid=false;
    bool rocIdentityMatches(uint32_t generation,const uint8_t source[6]) const {
        return rocSourceValid&&rocSourceGeneration==generation&&source&&!memcmp(rocSource,source,6);
    }
    bool requestedTargetValid=false;
    uint32_t requestedTarget=0,requestedGeneration=0;
    uint32_t retiredUid=0,retiredGeneration=0;bool retiredValid=false;
    uint8_t lastNotification[24]={},lastResponse[8]={};
    uint32_t notifyPhaseBefore=0,notifyEarlyBefore=0,responsePhaseBefore=0,responseEarlyBefore=0;
    bool alreadyRequested(uint32_t generation,uint32_t target) const {
        return requestedTargetValid&&requestedGeneration==generation&&requestedTarget==target;
    }

    uint32_t targetGp2=0, targetRevision=0, targetGeneration=0;
    uint64_t nextWakeNs=0;
    uint16_t targetAw=0;
    bool validationArmed=false,infraLost=false;
    bool targetValid=false, startAligned=false, windowDelivered=false, endDelivered=false;
    uint32_t psfSubmitted=0,mifSubmitted=0,psfCompleted=0,mifCompleted=0,lastSkip=0;
    uint8_t pendingSubtype=0;bool pendingSingleData=false;
    uint16_t lastLength=0,lastAw=0,lastDown=0;
    uint32_t lastTxGp2=0,lastStartGp2=0;
    uint8_t lastFrame[1536]={};
};
extern TemplateRadioState gTemplateRadio;
}
