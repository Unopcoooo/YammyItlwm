#include "../../itlwm/awdl/NativeQueueActivation.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
struct Queue{void *loop=nullptr;bool enabled=false,tx=false;};
using State=ItlAwdl::NativeQueueActivation<Queue>;
struct Ops{
 void *loop;State &state;unsigned fault=0,adds=0,removes=0,enables=0,disables=0;
 bool valid(Queue *q,bool tx){return q&&q->tx==tx;}
 void *workloop(Queue *q){return q->loop;}bool enabled(Queue *q){return q->enabled;}
 bool attach(Queue *q){++adds;if(fault==1&&q->tx)return false;q->loop=loop;return !(fault==2&&!q->tx);}
 bool detach(Queue *q){++removes;if(fault==6)return false;q->loop=nullptr;return true;}
 void enable(Queue *q,bool tx){++enables;
  if(fault==3&&tx)return;if(fault==4&&!tx)return;
  q->enabled=true;
  if(fault==5){state.requestStop();assert(!state.stop(*this));}
 }
 void disable(Queue *q,bool){++disables;if(fault!=7)q->enabled=false;}
};
int main(){
 int target=0,foreign=0;
 for(unsigned fault=0;fault<6;++fault){State state;Queue tx,rx;tx.tx=true;Ops ops{&target,state};ops.fault=fault;
  const bool started=state.start(ops,&tx,&rx);assert(started==(fault==0));
  if(started){assert(state.active&&tx.enabled&&rx.enabled);assert(!state.start(ops,&tx,&rx));assert(state.stop(ops));}
  assert(state.idle()&&!tx.loop&&!rx.loop&&!tx.enabled&&!rx.enabled);
 }
 // Existing queue state belongs to the registrant and must survive our stop.
 {State state;Queue tx{&target,true,true},rx{&target,true,false};Ops ops{&target,state};
  assert(state.start(ops,&tx,&rx)&&state.stop(ops));assert(!ops.adds&&!ops.removes&&!ops.enables&&!ops.disables);
  assert(tx.enabled&&rx.enabled&&tx.loop==&target&&rx.loop==&target);}
 {State state;Queue tx{&target,false,true},rx{&target,false,false};Ops ops{&target,state};
  assert(state.start(ops,&tx,&rx)&&state.stop(ops));assert(!ops.adds&&!ops.removes&&ops.enables==2&&ops.disables==2);
  assert(!tx.enabled&&!rx.enabled&&tx.loop==&target&&rx.loop==&target);}
 {State state;Queue tx{&foreign,true,true},rx;Ops ops{&target,state};
  assert(!state.start(ops,&tx,&rx)&&state.idle()&&tx.loop==&foreign&&tx.enabled&&!ops.adds);}
 // Failed teardown keeps ownership recorded; unbind/restart cannot erase it.
 for(unsigned fault:{6u,7u}){State state;Queue tx,rx;tx.tx=true;Ops ops{&target,state};
  assert(state.start(ops,&tx,&rx));ops.fault=fault;assert(!state.stop(ops)&&!state.idle());
  assert(!state.start(ops,&tx,&rx));ops.fault=0;assert(state.stop(ops)&&state.idle());}
 puts("PASS native queue activation: real ownership accounting, partial attach/enable rollback, reentrant stop, borrowed-state preservation, teardown failure/retry; ASan/UBSan, no RF");
}
