// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "DataFrames.hpp"
namespace ItlAwdl {
// Two samples, at most22 existing diagnostic rows. No packet mutation or allocation.
template<class Emit> bool lengthRejectDiagnostic(const uint8_t *p,size_t n,unsigned sample,Emit emit){
 if(!p||sample>=2||n<=kAwdlDataMax+2||n>4096)return false;
 emit(61,sample,unsigned(n),unsigned(dataLe16(p)),unsigned(dataLe16(p+24)));
 const auto part=[&](unsigned kind,size_t offset,size_t count){
  for(size_t i=0;i<count;i+=12){uint32_t words[3]={};
   for(size_t j=0;j<12&&i+j<count;++j)words[j/4]|=uint32_t(p[offset+i+j])<<((j%4)*8);
   emit(kind,(sample<<16)|unsigned(offset+i),words[0],words[1],words[2]);
  }
 };
 part(62,0,96);part(63,n-16,16);return true;
}
}
