// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "DataFrames.hpp"
namespace ItlAwdl {
// A newer OOB request supersedes even an unsupported or off-gate update.
// Only this serial is shared across gates. Bytes belong to the gated session.
#if defined(KERNEL) || defined(ITL_TEST_KEXT_REVISION)
// Apple kext compilation can give an inline function's local static a
// translation-unit-local identity. Both the setter and TX worker must refer
// to the same externally defined object.
extern uint64_t gNativeMifRevision;
inline uint64_t &oobRevision(){return gNativeMifRevision;}
#else
inline uint64_t &oobRevision(){static uint64_t value=0;return value;}
#endif
inline uint64_t invalidateOob(){return __atomic_add_fetch(&oobRevision(),1,__ATOMIC_SEQ_CST);}
inline uint64_t currentOobRevision(){return __atomic_load_n(&oobRevision(),__ATOMIC_SEQ_CST);}
struct NativeMifService {
    uint8_t bytes[224]={}; size_t size=0;
    uint64_t revision=0,expires=0,epoch=0;
    uint32_t generation=0,accepted=0,sent=0;
    struct Field { const uint8_t *data=nullptr; size_t size=0; };
    static bool label(const uint8_t *s,size_t n){
        if(!n||n>63)return false;
        for(size_t i=0;i<n;i++)if(s[i]<=32||s[i]>=127||s[i]=='.')return false;
        return true;
    }
    // Accept only the native AirDrop profile verified on 24H32. The record
    // names use Apple's fixed compression tokens, not offsets into this buffer.
    bool offer(const uint8_t *raw,size_t n,const uint8_t *local,uint64_t rev,
               uint64_t search,uint32_t gen,uint64_t now){
        size=0;
        if(!raw||!local||!search||!gen||!now||now>UINT64_MAX-200000000||
           n>1200||!n||rev==0||rev!=currentOobRevision())return false;
        Field host,ptr,srv,txt;bool own=false;size_t total=0;
        for(size_t i=0;i<n;){
            if(n-i<3)return false;
            unsigned type=raw[i];size_t length=dataLe16(raw+i+1);
            if(length>n-i-3)return false;
            const uint8_t *v=raw+i+3;i+=3+length;
            if(type==24){
                if(own||length!=40||memcmp(v,local,6)||memcmp(v+6,local,6))return false;
                own=true;
            }
            if(type!=16&&type!=2)continue;
            if(total+3+length>sizeof(bytes))return false;
            total+=3+length;
            Field *dst=&host;
            if(type==2){
                if(length<7)return false;
                size_t end=2+size_t(dataLe16(v));
                if(end<3||end+4>length||dataLe16(v+end)!=length-end-4||v[end+2]||v[end+3])return false;
                switch(v[end-1]){case 12:dst=&ptr;break;case 33:dst=&srv;break;case 16:dst=&txt;break;default:return false;}
            }
            if(dst->data)return false;
            dst->data=v;dst->size=length;
        }
        if(!own||!host.data||!ptr.data||!srv.data||!txt.data)return false;
        const auto h=host.data;const auto a=ptr.data;
        if(host.size<5||h[0]!=3||size_t(h[1])+4!=host.size||!label(h+2,h[1])||
           h[host.size-2]!=0xc0||h[host.size-1]!=12)return false;
        if(ptr.size<13||dataLe16(a)!=3||a[2]!=0xc0||a[3]!=7||
           size_t(a[9])+12!=ptr.size||!label(a+10,a[9])||a[ptr.size-2]!=0xc0||a[ptr.size-1])return false;
        const Field owners[]={srv,txt};
        for(Field f:owners){
            size_t key=1u+a[9]+2;
            if(dataLe16(f.data)!=key+1||f.size<key+7||memcmp(f.data+2,a+9,1u+a[9])||
               f.data[key]!=0xc0||f.data[key+1]!=7)return false;
        }
        auto body=[](Field f){return f.data+6+dataLe16(f.data);};
        const auto r=body(srv);const auto text=body(txt);
        size_t rn=srv.size-size_t(r-srv.data),tn=txt.size-size_t(text-txt.data);
        if(rn!=6+host.size-1||memcmp(r,"\0\0\0\0",4)||!dataBe16(r+4)||memcmp(r+6,h+1,host.size-1))return false;
        if(tn<8||size_t(text[0])+1!=tn||memcmp(text+1,"flags=",6))return false;
        for(size_t i=7;i<tn;i++)if(text[i]<'0'||text[i]>'9')return false;
        const Field fields[]={host,ptr,srv,txt};
        for(const auto &f:fields){memcpy(bytes+size,f.data-3,f.size+3);size+=f.size+3;}
        revision=rev;epoch=search;generation=gen;expires=now+200000000;++accepted;return true;
    }
    size_t take(uint8_t *frame,size_t used,size_t capacity,uint64_t search,uint32_t gen,uint64_t now){
        size_t count=size;size=0;
        if(!count||!frame||used>capacity||capacity-used<count||used<40||frame[30]!=3||
           revision!=currentOobRevision()||epoch!=search||generation!=gen||now>=expires||
           expires-now>200000000)return used;
        memcpy(frame+used,bytes,count);return used+count;
    }
};
}
