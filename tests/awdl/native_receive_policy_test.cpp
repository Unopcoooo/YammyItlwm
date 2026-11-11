#include "NativeSearchOwner.hpp"

#include <assert.h>

int main() {
    using namespace ItlAwdl;
    const uint8_t address[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};

    assert(nativeReceiveLimit(false) == 20000000000ULL);
    assert(nativeReceiveLimit(true) == 300000000000ULL);
    assert(!NativeSearchOwner().commit(1, address, 1, 300000000001ULL, true));

    NativeSearchOwner owner;
    assert(owner.commit(1, address, 1, nativeReceiveLimit(true), true));
    assert(owner.receiveValidation());
    assert(owner.deadlineNs - owner.startNs == 300000000000ULL);
    assert(owner.expire(1, owner.deadlineNs - 1) == false);
    assert(owner.expire(1, owner.deadlineNs));
}
