// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "../../itlwm/awdl/NativeBsdBinding.hpp"
struct Owner {int refs=1;void retain(){++refs;}void release(){assert(refs>1);--refs;}};
using Lease=ItlAwdl::NativeBsdBinding<Owner>;
struct Ops {
 void *actual=nullptr;int refs=0,error=0;unsigned prepares=0,finalizes=0;
 bool refFails=false,writeOnFailure=true,clear=true;Lease *cancel=nullptr;
 bool run=false,flagsFail=false;
 void *bound(Owner *){return actual;}
 bool retainInterface(void *){if(refFails)return false;++refs;return true;}
 void releaseInterface(void *){assert(refs>0);--refs;}
 bool running(void *){return run;}
 bool setRunning(void *,bool up){if(flagsFail)return false;run=up;return true;}
 int prepare(Owner *,void *p){++prepares;if(!error||writeOnFailure)actual=p;if(cancel)cancel->stop(*this);return error;}
 void finalize(Owner *,void *){++finalizes;if(cancel)cancel->stop(*this);if(clear)actual=nullptr;}
};
int main(){
 Owner owner;int net=0,other=0;
 {
  Lease l;Ops o;assert(l.start(o,&owner,&net)&&o.actual==&net&&owner.refs==2&&o.refs==1);
  assert(!l.start(o,&owner,&net));assert(l.stop(o)&&l.idle()&&owner.refs==1&&o.refs==0&&o.finalizes==1&&!o.actual);
 }
 for(bool mutate:{false,true}){
  Lease l;Ops o;o.error=5;o.writeOnFailure=mutate;
  assert(!l.start(o,&owner,&net)&&l.idle()&&owner.refs==1&&o.refs==0&&!o.actual);
  assert(o.finalizes==(mutate?1u:0u));
 }
 {
  Lease l;Ops o;o.actual=&net;
  assert(l.start(o,&owner,&net)&&!l.owned&&!o.prepares);
  assert(l.stop(o)&&o.actual==&net&&!o.finalizes&&owner.refs==1&&!o.refs);
 }
 {
  Lease l;Ops o;o.actual=&other;
  assert(!l.start(o,&owner,&net)&&owner.refs==1&&!o.refs&&o.actual==&other);
  o.actual=nullptr;o.refFails=true;assert(!l.start(o,&owner,&net)&&l.idle()&&!o.prepares);
 }
 {
  Lease l;Ops o;o.cancel=&l;
  assert(!l.start(o,&owner,&net)&&l.idle()&&owner.refs==1&&!o.refs&&o.finalizes==1);
 }
 {
  Lease l;Ops o;assert(l.start(o,&owner,&net));o.clear=false;
  assert(!l.stop(o)&&!l.idle()&&owner.refs==2&&o.refs==1);
  o.clear=true;assert(l.stop(o)&&l.idle()&&owner.refs==1&&!o.refs);
 }
 {
  Lease l;Ops o;assert(l.start(o,&owner,&net));o.actual=&other;
  assert(!l.stop(o)&&!o.finalizes&&owner.refs==2&&o.refs==1&&o.actual==&other);
  o.actual=&net;assert(l.stop(o)&&l.idle()&&owner.refs==1&&!o.refs);
 }
 {
  Lease l;Ops o;assert(l.start(o,&owner,&net)&&l.publishRunning(o)&&o.run&&l.runningOwned);
  o.flagsFail=true;assert(!l.stop(o)&&o.run&&owner.refs==2&&o.refs==1);
  o.flagsFail=false;assert(l.stop(o)&&!o.run&&l.idle()&&owner.refs==1&&!o.refs);
 }
 {
  Lease l;Ops o;o.run=true;assert(l.start(o,&owner,&net)&&l.publishRunning(o)&&!l.runningOwned);
  assert(l.stop(o)&&o.run&&owner.refs==1&&!o.refs);
 }
 puts("PASS native BSD lease: owned/borrowed references, partial prepare, cancel reentry, failed finalize retry and foreign binding quarantine");
}
