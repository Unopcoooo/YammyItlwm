// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace ItlAwdl {
// Borrow pre-existing queue state; undo only attachments/enables owned here.
template<class Object> struct NativeQueueActivation {
 Object *queues[2]={};void *loop=nullptr;
 unsigned attached=0,enabled=0;bool active=false,busy=false,cancelled=false;
 bool idle()const{return !active&&!busy&&!attached&&!enabled;}
 void requestStop(){cancelled=true;}
 template<class Ops> bool rollback(Ops &ops){
  bool ok=true;
  for(int i=1;i>=0;--i){const unsigned bit=1u<<i;auto q=queues[i];
   if(enabled&bit){ops.disable(q,i==0);if(ops.enabled(q))ok=false;else enabled&=~bit;}
   if((attached&bit)&&!(enabled&bit)){
    auto actual=ops.workloop(q);
    if(!actual)attached&=~bit;
    else if(actual!=loop||!ops.detach(q)||ops.workloop(q))ok=false;
    else attached&=~bit;
   }
  }
  active=false;
  if(!attached&&!enabled){queues[0]=queues[1]=nullptr;loop=nullptr;}
  return ok&&!attached&&!enabled;
 }
 template<class Ops> bool start(Ops &ops,Object *tx,Object *rx){
  if(!idle()||!ops.loop||!ops.valid(tx,true)||!ops.valid(rx,false))return false;
  queues[0]=tx;queues[1]=rx;loop=ops.loop;busy=true;cancelled=false;
  bool ok=true;
  for(unsigned i=0;i<2&&ok;++i){const unsigned bit=1u<<i;auto q=queues[i];auto previous=ops.workloop(q);
   if(previous&&previous!=loop){ok=false;break;}
   if(!previous){
    const bool added=ops.attach(q);
    const auto actual=ops.workloop(q);if(actual)attached|=bit;
    if(!added||actual!=loop){ok=false;break;}
   }
   if(cancelled){ok=false;break;}
   if(!ops.enabled(q)){enabled|=bit;ops.enable(q,i==0);}
   if(cancelled||!ops.enabled(q)||ops.workloop(q)!=loop)ok=false;
  }
  if(ok)active=true;else rollback(ops);
  busy=false;return ok;
 }
 template<class Ops> bool stop(Ops &ops){
  cancelled=true;if(busy)return false;
  if(idle())return true;
  if(ops.loop!=loop)return false;
  busy=true;const bool result=rollback(ops);busy=false;return result;
 }
};
}
