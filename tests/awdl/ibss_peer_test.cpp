#include "ibss-peer-contract.hpp"
#include "../../itlwm/awdl/IbssDataTx.hpp"
#include <assert.h>
#include <stdio.h>
#include <vector>
using namespace ItlIbss;
static PeerGrant grant(){PeerGrant g;g.base={1,1,1,3,4,3,9,true};g.generation=7;g.station=5;g.queue=10;g.color=2;g.local[0]=g.peer[0]=2;g.local[5]=1;g.peer[5]=2;g.independentlyReserved=true;return g;}
struct Transport {
 PeerState *state=nullptr;PeerCommands<Transport> *again=nullptr;
 unsigned calls=0,failAt=0,releases=0,flushes=0;bool healthy=true,drained=true,stopped=false,badStatus=false,changeAfter=false;
 std::vector<unsigned> events;std::vector<iwm_add_sta_cmd> commands;
 bool ready(const PeerGrant &g){return healthy&&g.generation==7;}
 int action(unsigned code){events.push_back(code);++calls;if(again){auto p=again;again=nullptr;assert(p->remove()==EBUSY);}if(changeAfter)healthy=false;return failAt==calls?EIO:0;}
 int status(unsigned id,const void *p,size_t n,uint32_t &status){assert(id==0x18&&n==sizeof(iwm_add_sta_cmd));commands.push_back(*static_cast<const iwm_add_sta_cmd*>(p));status=badStatus?0:1;return action(id);}
 int enableQueue(unsigned s,unsigned q,unsigned f,unsigned tid){assert(s==5&&q==10&&f==1&&!tid);return action(0x100);}
 int disableQueue(unsigned s,unsigned q,unsigned tid){assert(s==5&&q==10&&!tid&&drained);return action(0x101);}
 int drain(const PeerGrant &,bool on){return action(on?0x200:0x201);}
 int flush(uint32_t mask){assert(mask==(1u<<10));++flushes;return action(0x300);}
 bool queueDrained(const PeerGrant &){return drained;}
 int pdu(unsigned id,const void *p,size_t n){assert(id==0x19&&n==sizeof(iwm_rm_sta_cmd)&&static_cast<const iwm_rm_sta_cmd*>(p)->sta_id==5);return action(id);}
 void release(const PeerGrant &){assert(state&&!state->ready&&!state->stationMayExist&&!state->queueMayExist);++releases;}
 bool stopProof(const PeerGrant &){return stopped;}
};
int main(){
 auto g=grant();iwm_add_sta_cmd c;memset(&c,0xa5,sizeof(c));auto saved=c;auto bad=g;bad.queue=3;
 assert(!peerStationCommand(bad,false,c)&&!memcmp(&c,&saved,sizeof(c)));
 for(unsigned id:{0u,3u,4u,16u}){bad=g;bad.station=id;assert(!bad.valid());}
 bad=g;bad.independentlyReserved=false;assert(!bad.valid());bad=g;memcpy(bad.peer,bad.local,6);assert(!bad.valid());
 assert(peerStationCommand(g,false,c)&&!c.add_modify&&!c.modify_mask&&!c.station_type&&c.sta_id==5&&c.tid_disable_tx==65535&&c.mac_id_n_color==0x201&&!c.tfd_queue_msk&&!memcmp(c.addr,g.peer,6));
 assert(peerStationCommand(g,true,c)&&c.add_modify==1&&c.modify_mask==IWM_STA_MODIFY_QUEUES&&c.tfd_queue_msk==(1u<<10));
 PeerState s;Transport t;t.state=&s;PeerCommands<Transport> p(t,s);t.again=&p;
 assert(!p.create(g)&&s.ready&&t.events==std::vector<unsigned>({0x18,0x100,0x18}));assert(p.create(g)==EBUSY);
 t.drained=false;assert(p.remove()==EBUSY&&!s.ready&&s.queueMayExist&&s.stationMayExist&&!t.releases&&t.flushes==1);
 assert(p.remove()==EBUSY&&t.flushes==1);t.drained=true;assert(!p.remove()&&t.releases==1&&p.remove()==EINVAL);
 for(unsigned fail=1;fail<=3;++fail){PeerState x;Transport y;y.state=&x;y.failAt=fail;PeerCommands<Transport> z(y,x);
  assert(z.create(g)==EIO&&x.unknown&&x.stationMayExist&&!y.releases&&z.remove()==EIO);
  assert(!z.hardwareStopped());y.stopped=true;assert(z.hardwareStopped()&&y.releases==1&&!z.hardwareStopped());
 }
 for(unsigned fail=4;fail<=8;++fail){PeerState x;Transport y;y.state=&x;PeerCommands<Transport> z(y,x);assert(!z.create(g));y.failAt=fail;
  assert(z.remove()==EIO&&x.unknown&&!y.releases);y.stopped=true;assert(z.hardwareStopped()&&y.releases==1);
 }
 PeerState x;Transport y;y.state=&x;y.changeAfter=true;PeerCommands<Transport> z(y,x);assert(z.create(g)==EIO&&x.unknown&&!y.releases);
 PeerState v;Transport w;w.state=&v;w.badStatus=true;PeerCommands<Transport> u(w,v);assert(u.create(g)==EIO&&v.unknown&&!w.releases);
 // Observed HT flags are immutable grant identity; unknown bits cannot reach firmware.
 {auto observed=g;observed.capabilitiesObserved=true;observed.capabilityFlags=(1u<<28)|(3u<<19)|(7u<<23);
  iwm_add_sta_cmd wire={};assert(peerStationCommand(observed,false,wire));
  assert(wire.station_flags==observed.capabilityFlags&&wire.tid_disable_tx==0xffff);
  assert(!samePeerGrant(g,observed));observed.capabilityFlags|=1;assert(!peerStationCommand(observed,false,wire));}
 puts("PASS IBSS peer command core: real packed ADD_STA, LINK/nonaggregate/independent data queue, add-enable-modify order, reentry, failed/unknown operations retain ownership, flush ACK cannot release DMA, wait-drain-disable-remove-release, actual stop proof only. Hardware mocked; no live adapter.");
}
