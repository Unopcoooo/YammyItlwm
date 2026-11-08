#include "../../itlwm/awdl/AuxRoc.hpp"
#include <assert.h>
#include <stdio.h>
using namespace ItlAwdl;
int main() {
    uint8_t node[6]={2,1,2,3,4,5}; AuxRocCommand command;
    assert(buildAuxRocAdd(true,6,1,node,0x12345678,32,64,command));
    const uint8_t expected[44]={4,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,
        6,0,0,0,1,0,0,0,2,1,2,3,4,5,0,0,0x78,0x56,0x34,0x12,64,0,0,0,32,0,0,0};
    assert(command.length==44);
    for(unsigned i=0;i<44;++i) assert(command.bytes[i]==expected[i]);
    assert(buildAuxRocAdd(false,149,1,node,0,16,64,command));
    assert(command.length==40 && command.bytes[16]==0 && command.bytes[17]==149);
    assert(command.bytes[20]==2 && rocRead32(command.bytes+36)==16);
    assert(!buildAuxRocAdd(true,153,1,node,0,16,64,command) && !command.length);
    assert(!buildAuxRocAdd(true,6,1,node,0,65,64,command));
    assert(!buildAuxRocAdd(true,6,16,node,0,16,64,command));
    assert(!buildAuxRocAdd(true,6,1,nullptr,0,16,64,command));
    node[0]=3; assert(!buildAuxRocAdd(true,6,1,node,0,16,64,command));
    command=buildAuxRocRemove(true,0x12345678);
    assert(command.length==44 && command.bytes[4]==3 && rocRead32(command.bytes+8)==0x12345678);
    uint8_t notification[24]={}; rocWrite32(notification,0xfffffff0);
    rocWrite32(notification+8,42); rocWrite32(notification+12,4);
    rocWrite32(notification+16,1); rocWrite32(notification+20,1);
    uint8_t response[8]={42,0,0,0,0,0,0,0};
    AuxRocSession session; session.resetAfterDeviceQuiesced(7);
    assert(!session.mayTransmit(7) && !session.begin(6) && session.begin(7));
    assert(session.response(response,8,7) && !session.mayTransmit(7));
    assert(!session.notify(notification,24,6));
    assert(session.notify(notification,24,7) && session.mayTransmit(7) && session.startGp2==0xfffffff0);
    assert(!session.begin(7));
    assert(session.requestStop() && !session.mayTransmit(7));
    assert(!session.queueDrained()); // an accepted remove command alone is not an END
    rocWrite32(notification+16,2);
    assert(session.notify(notification,24,7) && session.phase==RocPhase::Draining);
    assert(!session.begin(7) && session.queueDrained() && session.begin(7));
    assert(!session.response(response,7,7) && session.phase==RocPhase::Quarantined);
    assert(!session.begin(7) && !session.queueDrained());
    // Early START and END are bounded and replayed only for the accepted UID.
    session.resetAfterDeviceQuiesced(8); assert(session.begin(8));
    rocWrite32(notification+16,1);
    assert(session.notify(notification,24,8) && !session.mayTransmit(8));
    rocWrite32(notification+16,2); assert(session.notify(notification,24,8));
    assert(session.response(response,8,8) && session.phase==RocPhase::Draining && session.queueDrained());
    assert(session.begin(8)); rocWrite32(notification+16,1); rocWrite32(notification+20,0);
    assert(session.response(response,8,8));
    assert(session.notify(notification,24,8) && !session.mayTransmit(8)); // failed START
    assert(session.queueDrained());
    assert(session.begin(8)); session.uncertainFailure(); assert(!session.mayTransmit(8) && !session.begin(8));
    session.resetAfterDeviceQuiesced(9); assert(session.begin(9));
    response[4]=1; assert(!session.response(response,8,9) && session.phase==RocPhase::Idle);
    RocNotification result;
    for(unsigned n=0;n<24;++n) assert(!decodeRocNotification(notification,n,result));
    assert(!decodeRocNotification(nullptr,24,result));
    session.resetAfterDeviceQuiesced(10); response[4]=0;
    assert(session.begin(10) && session.response(response,8,10));
    rocWrite32(notification+20,1); rocWrite32(notification+16,1);
    rocWrite32(notification+8,43);
    assert(!session.notify(notification,24,10) && !session.mayTransmit(10));
    rocWrite32(notification+8,42); rocWrite32(notification+12,0);
    assert(!session.notify(notification,24,10) && !session.mayTransmit(10));
    session.resetAfterDeviceQuiesced(11); assert(session.begin(11));
    rocWrite32(notification+12,4);
    assert(session.notify(notification,24,11) && session.notify(notification,24,11));
    assert(!session.notify(notification,24,11) && session.phase==RocPhase::Quarantined);
    assert(!session.response(response,8,11));
    puts("AUX ROC: independent wire fixture, UID/generation matching, early events and cleanup gates PASS");
}
