// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace ItlAwdl {
// The legacy bridge owns its ifnet. Pin it while the native interface borrows
// it through prepare/finalize; never write a reconstructed expansion pointer.
template<class Object> struct NativeBsdBinding {
 Object *owner=nullptr;void *interface=nullptr;
 bool owned=false,busy=false,cancelled=false,runningOwned=false;
 bool idle()const{return !owner&&!busy;}
 template<class Ops> bool rollback(Ops &ops){
  if(!owner)return true;
  const auto current=ops.bound(owner);
  if(owned&&current&&current!=interface)return false;
  if(runningOwned){
   const bool changed=ops.setRunning(interface,false);
   if(!changed||ops.running(interface))return false;
   runningOwned=false;
  }
  const auto afterFlags=ops.bound(owner);
  if(owned&&afterFlags&&afterFlags!=interface)return false;
  if(owned&&afterFlags==interface)ops.finalize(owner,interface);
  if(owned&&ops.bound(owner))return false;
  auto oldOwner=owner;auto oldInterface=interface;
  owner=nullptr;interface=nullptr;owned=false;
  ops.releaseInterface(oldInterface);oldOwner->release();return true;
 }
 template<class Ops> bool start(Ops &ops,Object *object,void *ifnet){
  if(!idle()||!object||!ifnet)return false;
  const auto current=ops.bound(object);
  if((current&&current!=ifnet)||!ops.retainInterface(ifnet))return false;
  object->retain();owner=object;interface=ifnet;busy=true;cancelled=false;
  bool ok=true;
  if(!current){owned=true;ok=ops.prepare(object,ifnet)==0;}
  ok=ok&&!cancelled&&ops.bound(object)==ifnet;
  if(!ok)rollback(ops);
  busy=false;return ok;
 }
 template<class Ops> bool publishRunning(Ops &ops){
  if(!owner||busy||cancelled||ops.bound(owner)!=interface)return false;
  busy=true;bool ok=true;
  if(!ops.running(interface)){runningOwned=true;ok=ops.setRunning(interface,true);}
  ok=ok&&!cancelled&&ops.running(interface);busy=false;return ok;
 }
 template<class Ops> bool stop(Ops &ops){
  cancelled=true;if(busy)return false;
  busy=true;const bool result=rollback(ops);busy=false;return result;
 }
};
}
