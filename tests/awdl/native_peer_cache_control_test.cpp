#include "NativePeerCacheControl.hpp"
#include <assert.h>
#include <initializer_list>
#include <stdint.h>
#include <string.h>

static void setRequest(uint8_t *request, uint32_t opcode, const uint8_t *address)
{
    memset(request, 0, ItlAwdl::NativePeerCacheControl::Size);
    memcpy(request + ItlAwdl::NativePeerCacheControl::OpcodeOffset,
           &opcode, sizeof(opcode));
    memcpy(request + ItlAwdl::NativePeerCacheControl::AddressOffset,
           address, ItlAwdl::NativePeerCacheControl::AddressLength);
}

int main()
{
    uint8_t request[ItlAwdl::NativePeerCacheControl::Size];
    const uint8_t peer[] = {0xb6, 0x07, 0xa1, 0x02, 0x11, 0xd5};
    uint32_t operation = UINT32_MAX;
    uint8_t address[ItlAwdl::NativePeerCacheControl::AddressLength] = {};

    for (uint32_t opcode : {0u, 2u, 6u}) {
        setRequest(request, opcode, peer);
        assert(ItlAwdl::NativePeerCacheControl::decode(
            request, sizeof(request), operation, address));
        assert(operation == opcode && !memcmp(address, peer, sizeof(peer)));
        uint8_t completion[ItlAwdl::NativePeerCacheControl::Size] = {};
        request[0xe] = 0x34;
        request[0xf] = 0x12;
        assert(ItlAwdl::NativePeerCacheControl::makeCompletion(
            request, sizeof(request), completion));
        uint32_t completed = 0;
        memcpy(&completed, completion + ItlAwdl::NativePeerCacheControl::OpcodeOffset,
               sizeof(completed));
        assert(completed == (opcode == ItlAwdl::NativePeerCacheControl::Delete ?
            ItlAwdl::NativePeerCacheControl::DeleteCompleted :
            ItlAwdl::NativePeerCacheControl::AddCompleted));
        assert(!memcmp(completion + ItlAwdl::NativePeerCacheControl::AddressOffset,
                       peer, sizeof(peer)));
        assert(completion[0xe] == 0x34 && completion[0xf] == 0x12);
    }
    setRequest(request, 1, peer);
    assert(!ItlAwdl::NativePeerCacheControl::decode(
        request, sizeof(request), operation, address));
    setRequest(request, 0, peer);
    request[ItlAwdl::NativePeerCacheControl::AddressOffset] |= 1;
    assert(!ItlAwdl::NativePeerCacheControl::decode(
        request, sizeof(request), operation, address));
    memset(request, 0, sizeof(request));
    assert(!ItlAwdl::NativePeerCacheControl::decode(
        request, sizeof(request), operation, address));
    assert(!ItlAwdl::NativePeerCacheControl::decode(
        request, sizeof(request) - 1, operation, address));
    assert(!ItlAwdl::NativePeerCacheControl::decode(
        nullptr, sizeof(request), operation, address));
}
