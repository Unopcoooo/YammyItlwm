#include <cassert>
#include <cstdio>
#include "NativeTxTerminal.hpp"
using ItlAwdl::nativeTxLateSuccess;
int main(){
 const bool base=nativeTxLateSuccess(false,false,false,false,20,20,1,1,0,1,0,0,0,0x201);
 assert(base);
 assert(!nativeTxLateSuccess(true,false,false,false,20,20,1,1,0,1,0,0,0,0x201));
 assert(!nativeTxLateSuccess(false,true,false,false,20,20,1,1,0,1,0,0,0,0x201));
 assert(!nativeTxLateSuccess(false,false,true,false,20,20,1,1,0,1,0,0,0,0x201));
 assert(!nativeTxLateSuccess(false,false,false,true,20,20,1,1,0,1,0,0,0,0x201));
 assert(!nativeTxLateSuccess(false,false,false,false,20,21,1,1,0,1,0,0,0,0x201));
 assert(!nativeTxLateSuccess(false,false,false,false,20,20,0,1,0,1,0,0,0,0x201));
 assert(!nativeTxLateSuccess(false,false,false,false,20,20,1,0,0,1,0,0,0,0x201));
 assert(!nativeTxLateSuccess(false,false,false,false,20,20,1,1,1,1,0,0,0,0x201));
 assert(!nativeTxLateSuccess(false,false,false,false,20,20,1,1,0,0,0,0,0,0x201));
 assert(!nativeTxLateSuccess(false,false,false,false,20,20,1,1,0,1,1,0,0,0x201));
 assert(!nativeTxLateSuccess(false,false,false,false,20,20,1,1,0,1,0,1,0,0x201));
 assert(!nativeTxLateSuccess(false,false,false,false,20,20,1,1,0,1,0,0,1,0x201));
 assert(!nativeTxLateSuccess(false,false,false,false,20,20,1,1,0,1,0,0,0,0x8e));
 assert(nativeTxLateSuccess(false,false,false,false,20,20,1,1,0,1,0,0,0,0x101));
 puts("PASS late TX success requires matching generation, completion, reclaim and success status");
}
