// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "NativePeerRegistration.hpp"
namespace ItlAwdl {
// Offline candidate: wire to the live station/queue inventory before use.
// A queue is never reassigned within a firmware generation: TX notifications
// carry queue/slot, not our peer identity. Retired entries remain tombstones.
struct NativePeerInventory {
 static constexpr unsigned FirstStation=5,StationEnd=16,FirstQueue=10,QueueEnd=18;
 static constexpr unsigned Capacity=QueueEnd-FirstQueue;
 struct Entry {
  NativePeerKey key;
  uint8_t station=0,queue=0;
  bool used=false,retiring=false,released=false;
 };
 Entry entries[Capacity];
 uint32_t generation=0,modelEpoch=0;
 uint16_t stationMask=0;uint32_t queueMask=0;
 bool begun=false,closed=false;
 bool begin(uint32_t gen,uint32_t model,uint16_t provenFreeStations,uint32_t provenFreeQueues){
  if(begun||!gen||!model)return false;
  generation=gen;modelEpoch=model;stationMask=provenFreeStations;queueMask=provenFreeQueues;begun=true;return true;
 }
 const Entry *find(const NativePeerKey &key)const{
  if(!begun||!key.valid()||key.generation!=generation||key.epoch!=modelEpoch)return nullptr;
  for(const auto &e:entries)if(e.used&&!memcmp(e.key.mac,key.mac,6)&&!memcmp(e.key.ip,key.ip,16))return &e;
  return nullptr;
 }
 bool refreshEpoch(const NativePeerKey &key){
  if(closed||!begun||!key.valid()||key.generation!=generation)return false;
  for(auto &e:entries)if(e.used&&!memcmp(e.key.mac,key.mac,6)&&!memcmp(e.key.ip,key.ip,16)){
   e.key.epoch=key.epoch;return true;
  }
  return false;
 }
 const Entry *reserve(const NativePeerKey &key){
  if(closed||!begun||!key.valid()||key.generation!=generation||key.epoch!=modelEpoch)return nullptr;
  if(auto e=find(key))return e->retiring||e->released?nullptr:e;
  unsigned station=FirstStation,queue=FirstQueue;
  while(station<StationEnd&&!(stationMask&(1u<<station)))++station;
  while(queue<QueueEnd&&!(queueMask&(1u<<queue)))++queue;
  if(station==StationEnd||queue==QueueEnd)return nullptr;
  for(auto &e:entries)if(!e.used){
   e.key=key;e.station=uint8_t(station);e.queue=uint8_t(queue);e.used=true;
   stationMask&=uint16_t(~(1u<<station));queueMask&=~(1u<<queue);return &e;
  }
  return nullptr;
 }
 const Entry *completionOwner(uint32_t gen,unsigned queue)const{
  if(gen!=generation)return nullptr;
  for(const auto &e:entries)if(e.used&&e.queue==queue)return &e;
  return nullptr;
 }
 bool retire(const NativePeerKey &key){
  auto e=const_cast<Entry*>(find(key));if(!e)return false;e->retiring=true;return true;
 }
 // Caller must obtain these facts from actual port/registration teardown.
 // A flush ACK is deliberately not an input or a release proof.
 bool acknowledgeRelease(const NativePeerKey &key,bool dmaDrained,bool stationRemoved,bool queueDisabled,bool neighborWithdrawn){
  auto e=const_cast<Entry*>(find(key));
  if(!e||!e->retiring||e->released||!dmaDrained||!stationRemoved||!queueDisabled||!neighborWithdrawn)return false;
  e->released=true;return true;
 }
 void close(){closed=true;for(auto &e:entries)if(e.used)e.retiring=true;}
 bool allReleased()const{
  if(!closed)return false;
  for(const auto &e:entries)if(e.used&&!e.released)return false;
  return true;
 }
 // No reset/reuse API: destroy with its owning, fully retired hardware epoch.
};
}
